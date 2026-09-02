#include "backlight.h"
#include "display_port.h"
#include "st7701_commands.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#define DISPLAY_H_RES 480
#define DISPLAY_V_RES 640
#define DISPLAY_LVGL_DRAW_ROWS 40
#define DISPLAY_RESET_GPIO GPIO_NUM_40

static const char *TAG = "display_port";
static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_display;
static void *s_draw_buffers[2];

static esp_err_t create_dsi_bus(esp_lcd_dsi_bus_handle_t *bus_out) {
  const esp_lcd_dsi_bus_config_t config = {
      .bus_id = 0,
      .num_data_lanes = 2,
      .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
      .lane_bit_rate_mbps = 500,
  };
  return esp_lcd_new_dsi_bus(&config, bus_out);
}

static esp_err_t create_panel_io(esp_lcd_dsi_bus_handle_t bus,
                                 esp_lcd_panel_io_handle_t *io_out) {
  const esp_lcd_dbi_io_config_t config = {
      .virtual_channel = 0,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  return esp_lcd_new_panel_io_dbi(bus, &config, io_out);
}

static esp_err_t create_st7701_panel(esp_lcd_dsi_bus_handle_t bus,
                                     esp_lcd_panel_io_handle_t io,
                                     esp_lcd_panel_handle_t *panel_out) {
  size_t command_count;
  const st7701_lcd_init_cmd_t *commands = st7701_init_commands(&command_count);
  const esp_lcd_dpi_panel_config_t dpi_config = {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = 20,
      .out_color_format = LCD_COLOR_FMT_RGB888,
      .in_color_format = LCD_COLOR_FMT_RGB888,
      .video_timing =
          {
              .h_size = DISPLAY_H_RES,
              .v_size = DISPLAY_V_RES,
              .hsync_pulse_width = 2,
              .hsync_back_porch = 6,
              .hsync_front_porch = 14,
              .vsync_pulse_width = 2,
              .vsync_back_porch = 12,
              .vsync_front_porch = 8,
          },
  };
  st7701_vendor_config_t vendor_config = {
      .init_cmds = commands,
      .init_cmds_size = command_count,
      .mipi_config =
          {
              .dsi_bus = bus,
              .dpi_config = &dpi_config,
          },
      .flags =
          {
              .use_mipi_interface = 1,
          },
  };
  esp_lcd_panel_dev_config_t config = {
      .reset_gpio_num = DISPLAY_RESET_GPIO,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 24,
      .vendor_config = &vendor_config,
  };
  return esp_lcd_new_panel_st7701(io, &config, panel_out);
}

static esp_err_t panel_init(esp_lcd_panel_handle_t *panel_out) {
  ESP_RETURN_ON_FALSE(panel_out != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "Panel output handle is required");

  esp_ldo_channel_handle_t ldo_channel;
  const esp_ldo_channel_config_t ldo_config = {
      .chan_id = 3,
      .voltage_mv = 2500,
  };
  ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_config, &ldo_channel), TAG,
                      "Failed to power MIPI DSI PHY");

  esp_lcd_dsi_bus_handle_t dsi_bus;
  ESP_RETURN_ON_ERROR(create_dsi_bus(&dsi_bus), TAG,
                      "Failed to create DSI bus");

  esp_lcd_panel_io_handle_t panel_io;
  ESP_RETURN_ON_ERROR(create_panel_io(dsi_bus, &panel_io), TAG,
                      "Failed to create DBI panel IO");

  ESP_RETURN_ON_ERROR(create_st7701_panel(dsi_bus, panel_io, panel_out), TAG,
                      "Failed to create ST7701 panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel_out), TAG,
                      "Failed to reset panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel_out), TAG,
                      "Failed to initialize panel");
  return backlight_init();
}

static uint32_t lvgl_tick_get_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area,
                          uint8_t *color_map) {
  esp_err_t ret = esp_lcd_panel_draw_bitmap(
      s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LVGL display flush failed: %s", esp_err_to_name(ret));
    lv_display_flush_ready(display);
  }
}

static bool color_transfer_done(esp_lcd_panel_handle_t panel,
                                esp_lcd_dpi_panel_event_data_t *event,
                                void *user_ctx) {
  (void)panel;
  (void)event;
  lv_display_flush_ready(user_ctx);
  return false;
}

static esp_err_t lvgl_init(esp_lcd_panel_handle_t panel) {
  lv_init();
  lv_tick_set_cb(lvgl_tick_get_ms);

  const size_t buffer_size = DISPLAY_H_RES * DISPLAY_LVGL_DRAW_ROWS *
                             lv_color_format_get_size(LV_COLOR_FORMAT_RGB888);
  for (size_t i = 0; i < 2; ++i) {
    s_draw_buffers[i] =
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buffer_size,
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_draw_buffers[i] != NULL, ESP_ERR_NO_MEM, TAG,
                        "Failed to allocate LVGL draw buffer");
  }

  s_display = lv_display_create(DISPLAY_H_RES, DISPLAY_V_RES);
  ESP_RETURN_ON_FALSE(s_display != NULL, ESP_ERR_NO_MEM, TAG,
                      "Failed to create LVGL display");
  lv_display_set_default(s_display);
  lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB888);
  lv_display_set_buffers(s_display, s_draw_buffers[0], s_draw_buffers[1],
                         buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s_display, lvgl_flush_cb);

  const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
      .on_color_trans_done = color_transfer_done,
  };
  return esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks,
                                                     s_display);
}

esp_err_t display_port_init(void) {
  ESP_RETURN_ON_ERROR(panel_init(&s_panel), TAG,
                      "Display panel initialization failed");
  return lvgl_init(s_panel);
}

esp_err_t display_port_sleep(void) {
  ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_INVALID_STATE, TAG,
                      "Display panel is not initialized");
  ESP_RETURN_ON_ERROR(backlight_set_enabled(false), TAG,
                      "Failed to turn off backlight");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, false), TAG,
                      "Failed to turn panel off");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_sleep(s_panel, true), TAG,
                      "Failed to put panel to sleep");
  return ESP_OK;
}

uint32_t display_port_timer_handler(void) {
  esp_err_t ret = backlight_service();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Backlight service failed: %s", esp_err_to_name(ret));
  }
  return lv_timer_handler();
}
