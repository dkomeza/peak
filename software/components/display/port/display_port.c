#include "display_port.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "display_port";
#define DISPLAY_H_RES 480
#define DISPLAY_V_RES 640
#define DISPLAY_LVGL_DRAW_ROWS 40

static esp_lcd_panel_handle_t s_dpi_panel;
static lv_display_t *s_lvgl_display;
static void *s_lvgl_draw_buffers[2];

uint32_t display_cpu_idle_percent(void) {
  static configRUN_TIME_COUNTER_TYPE previous_idle_time;
  static int64_t previous_sample_us;

  const int64_t sample_us = esp_timer_get_time();
  const configRUN_TIME_COUNTER_TYPE idle_time = ulTaskGetIdleRunTimeCounter();

  if (previous_sample_us == 0) {
    previous_sample_us = sample_us;
    previous_idle_time = idle_time;
    return 100;
  }

  const uint64_t elapsed_capacity =
      (uint64_t)(sample_us - previous_sample_us) * configNUMBER_OF_CORES;
  const uint64_t idle_delta = idle_time - previous_idle_time;
  previous_sample_us = sample_us;
  previous_idle_time = idle_time;

  if (elapsed_capacity == 0) {
    return 0;
  }

  return (uint32_t)((LV_MIN(idle_delta, elapsed_capacity) * 100U) /
                    elapsed_capacity);
}

static const st7701_lcd_init_cmd_t init_cmds[] = {
    // --- Page 3 ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},

    // --- Page 0 (Gamma & Display Line Setting) ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x4F, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x10, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x31, 0x02}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xB0,
     (uint8_t[]){0x00, 0x10, 0x17, 0x0D, 0x11, 0x06, 0x05, 0x08, 0x07, 0x1F,
                 0x04, 0x11, 0x0E, 0x29, 0x30, 0x1F},
     16, 0}, // Positive Gamma
    {0xB1,
     (uint8_t[]){0x00, 0x0D, 0x14, 0x0E, 0x11, 0x06, 0x04, 0x08, 0x08, 0x20,
                 0x05, 0x13, 0x13, 0x26, 0x30, 0x1F},
     16, 0}, // Negative Gamma

    // --- Page 1 (Power & GIP Setting) ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x65}, 1, 0}, // Vop
    {0xB1, (uint8_t[]){0x7C}, 1, 0}, // VCOM
    {0xB2, (uint8_t[]){0x87}, 1, 0}, // VGH = +15V
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x40}, 1, 0}, // VGL = -10V
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x20}, 1, 0}, // AVDD & AVCL
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xEE, (uint8_t[]){0x42}, 1, 0},
    // GIP Settings
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
    {0xE1,
     (uint8_t[]){0x04, 0xA0, 0x06, 0xA0, 0x05, 0xA0, 0x07, 0xA0, 0x00, 0x44,
                 0x44},
     11, 0},
    {0xE2,
     (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00, 0x00},
     12, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x22, 0x22}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5,
     (uint8_t[]){0x0C, 0x90, 0xA0, 0xA0, 0x0E, 0x92, 0xA0, 0xA0, 0x08, 0x8C,
                 0xA0, 0xA0, 0x0A, 0x8E, 0xA0, 0xA0},
     16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x22, 0x22}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8,
     (uint8_t[]){0x0D, 0x91, 0xA0, 0xA0, 0x0F, 0x93, 0xA0, 0xA0, 0x09, 0x8D,
                 0xA0, 0xA0, 0x0B, 0x8F, 0xA0, 0xA0},
     16, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0xE4, 0xE4, 0x44, 0x00, 0x00}, 7, 0},
    {0xED,
     (uint8_t[]){0xFF, 0xF5, 0x47, 0x6F, 0x0B, 0xA1, 0xAB, 0xFF, 0xFF, 0xBA,
                 0x1A, 0xB0, 0xF6, 0x74, 0x5F, 0xFF},
     16, 0},
    {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x40, 0x3F, 0x64}, 6, 0},

    // --- Return to Page 0 ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},

    // --- Page 3 Wake Sequence ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE6, (uint8_t[]){0x7C}, 1, 0},
    {0xE8, (uint8_t[]){0x00, 0x0E}, 2, 0},

    // --- Return to Page 0 & Sleep Out ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120}, // CRITICAL DELAY

    // --- Page 3 Pump Sequence ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE8, (uint8_t[]){0x00, 0x0C}, 2, 10}, // CRITICAL DELAY
    {0xE8, (uint8_t[]){0x00, 0x00}, 2, 0},

    // --- Final Initialization (Page 0) ---
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},  // TE Off
    {0x3A, (uint8_t[]){0x70}, 1, 0},  // 24-bit RGB Interface
    {0x29, (uint8_t[]){0x00}, 0, 25}, // Display ON + Delay
};

static esp_err_t panel_init(esp_lcd_panel_handle_t *panel_out) {
  ESP_RETURN_ON_FALSE(panel_out != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "Panel output handle is required");

  // LDO Power
  esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
  esp_ldo_channel_config_t ldo_cfg = {
      .chan_id = 3,
      .voltage_mv = 2500,
  };
  ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy), TAG,
                      "Failed to power MIPI DSI PHY");

  // 1. DSI bus
  ESP_LOGI(TAG, "Initializing DSI bus...");
  esp_lcd_dsi_bus_handle_t dsi_bus;
  esp_lcd_dsi_bus_config_t dsi_bus_cfg = {
      .bus_id = 0,
      .num_data_lanes = 2,
      .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
      .lane_bit_rate_mbps = 500,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi_bus_cfg, &dsi_bus), TAG,
                      "Failed to create DSI bus");
  ESP_LOGI(TAG, "DSI bus created successfully!");

  // 2. DBI panel IO (command channel)
  esp_lcd_panel_io_handle_t dbi_io;
  esp_lcd_dbi_io_config_t dbi_cfg = {
      .virtual_channel = 0,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io), TAG,
                      "Failed to create DBI panel IO");
  ESP_LOGI(TAG, "DBI panel IO created successfully!");

  // 3. DPI panel configuration
  esp_lcd_dpi_panel_config_t dpi_cfg = {
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

  // 4. ST7701 vendor config — for MIPI mode, dsi_bus and dpi_config
  st7701_vendor_config_t vendor_cfg = {
      .init_cmds = init_cmds,
      .init_cmds_size = sizeof(init_cmds) / sizeof(init_cmds[0]),
      .mipi_config =
          {
              .dsi_bus = dsi_bus,
              .dpi_config = &dpi_cfg,
          },
      .flags =
          {
              .use_mipi_interface = 1,
              .mirror_by_cmd = 0,
          },
  };
  esp_lcd_panel_dev_config_t panel_dev_cfg = {
      .reset_gpio_num = 40, // RST pin
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 24,
      .vendor_config = &vendor_cfg,
  };

  esp_lcd_panel_handle_t dpi_panel;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_st7701(dbi_io, &panel_dev_cfg, &dpi_panel), TAG,
      "Failed to create ST7701 panel");
  ESP_LOGI(TAG, "ST7701 panel created successfully!");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(dpi_panel), TAG,
                      "Failed to reset panel");
  ESP_LOGI(TAG, "Panel reset successfully!");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(dpi_panel), TAG,
                      "Failed to initialize panel");
  ESP_LOGI(TAG, "Panel initialized successfully!");

  // Backlight enable
  ESP_RETURN_ON_ERROR(gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT), TAG,
                      "Failed to configure backlight GPIO");
  ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_10, 1), TAG,
                      "Failed to enable backlight");

  *panel_out = dpi_panel;
  return ESP_OK;
}

static uint32_t lvgl_tick_get_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area,
                          uint8_t *color_map) {
  esp_err_t ret = esp_lcd_panel_draw_bitmap(
      s_dpi_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LVGL display flush failed: %s", esp_err_to_name(ret));
    lv_display_flush_ready(display);
  }
}

static bool lvgl_color_trans_done_cb(esp_lcd_panel_handle_t panel,
                                     esp_lcd_dpi_panel_event_data_t *event,
                                     void *user_ctx) {
  lv_display_t *display = user_ctx;
  lv_display_flush_ready(display);
  return false;
}

static esp_err_t lvgl_init(esp_lcd_panel_handle_t panel) {
  ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "LVGL requires a display panel");

  lv_init();
  lv_tick_set_cb(lvgl_tick_get_ms);

  const size_t draw_buffer_size =
      DISPLAY_H_RES * DISPLAY_LVGL_DRAW_ROWS *
      lv_color_format_get_size(LV_COLOR_FORMAT_RGB888);

  for (size_t i = 0; i < 2; ++i) {
    s_lvgl_draw_buffers[i] =
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, draw_buffer_size,
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_lvgl_draw_buffers[i] == NULL) {
      ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer %u", (unsigned)i);
      return ESP_ERR_NO_MEM;
    }
  }

  s_lvgl_display = lv_display_create(DISPLAY_H_RES, DISPLAY_V_RES);
  ESP_RETURN_ON_FALSE(s_lvgl_display != NULL, ESP_ERR_NO_MEM, TAG,
                      "Failed to create LVGL display");
  lv_display_set_default(s_lvgl_display);
  lv_display_set_color_format(s_lvgl_display, LV_COLOR_FORMAT_RGB888);
  lv_display_set_buffers(s_lvgl_display, s_lvgl_draw_buffers[0],
                         s_lvgl_draw_buffers[1], draw_buffer_size,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s_lvgl_display, lvgl_flush_cb);

  const esp_lcd_dpi_panel_event_callbacks_t panel_callbacks = {
      .on_color_trans_done = lvgl_color_trans_done_cb,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(
                          panel, &panel_callbacks, s_lvgl_display),
                      TAG, "Failed to register LVGL panel callback");

  ESP_LOGI(TAG, "LVGL initialized for %dx%d RGB888 MIPI DSI display",
           DISPLAY_H_RES, DISPLAY_V_RES);

  return ESP_OK;
}

esp_err_t display_port_init(void) {
  ESP_RETURN_ON_ERROR(panel_init(&s_dpi_panel), TAG,
                      "Display panel initialization failed");
  return lvgl_init(s_dpi_panel);
}

uint32_t display_port_timer_handler(void) { return lv_timer_handler(); }
