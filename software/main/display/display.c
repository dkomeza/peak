#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "lvgl.h"
#include "tick/lv_tick.h"
#include <esp_timer.h>
#include <string.h>

static const char *TAG = "PEAK";

esp_lcd_panel_handle_t dpi_panel;

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

esp_lcd_panel_handle_t init(void) {
  // Power up VDD_MIPI_DPHY via LDO channel 3 (2.5V).
  // Must happen before esp_lcd_new_dsi_bus() or the PHY PLL never locks.
  esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
  esp_ldo_channel_config_t ldo_cfg = {
      .chan_id = 3,
      .voltage_mv = 2500,
  };
  ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy));

  // 1. DSI bus
  ESP_LOGI(TAG, "Initializing DSI bus...");
  esp_lcd_dsi_bus_handle_t dsi_bus;
  esp_lcd_dsi_bus_config_t dsi_bus_cfg = {
      .bus_id = 0,
      .num_data_lanes = 2,
      .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
      .lane_bit_rate_mbps = 500,
  };
  ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&dsi_bus_cfg, &dsi_bus));
  ESP_LOGI(TAG, "DSI bus created successfully!");

  // 2. DBI panel IO (command channel)
  esp_lcd_panel_io_handle_t dbi_io;
  esp_lcd_dbi_io_config_t dbi_cfg = {
      .virtual_channel = 0,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io));
  ESP_LOGI(TAG, "DBI panel IO created successfully!");

  // 3. DPI panel configuration
  // Timing values are typical defaults for 480x640 @ ~60fps.
  // HFP/HBP/HSYNC and VFP/VBP/VSYNC may need tuning for your specific panel.
  // GPIO 39 (TE) is available for vsync-locked updates; not used in this test.
  esp_lcd_dpi_panel_config_t dpi_cfg = {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = 20, // Matches manufacturer RGB_CLOCK
      .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888, // 24-bit
      .video_timing =
          {
              .h_size = 480,
              .v_size = 640,
              .hsync_pulse_width = 2,
              .hsync_back_porch = 12,
              .hsync_front_porch = 8,
              .vsync_pulse_width = 2,
              .vsync_back_porch = 12,
              .vsync_front_porch = 8,
          },
      .flags.use_dma2d = 1,
  };

  // 4. ST7701 vendor config — for MIPI mode, dsi_bus and dpi_config
  //    must be set so the driver can create the DPI panel internally.
  //    esp_lcd_new_panel_st7701() returns the DPI panel handle.
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

  // esp_lcd_new_panel_st7701() with use_mipi_interface=1 calls the MIPI
  // variant internally, which creates the DPI panel and returns its handle.
  esp_lcd_panel_handle_t dpi_panel;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(dbi_io, &panel_dev_cfg, &dpi_panel));
  ESP_LOGI(TAG, "ST7701 panel created successfully!");
  ESP_ERROR_CHECK(esp_lcd_panel_reset(dpi_panel));
  ESP_LOGI(TAG, "Panel reset successfully!");
  ESP_ERROR_CHECK(esp_lcd_panel_init(dpi_panel));
  ESP_LOGI(TAG, "Panel initialized successfully!");

  // Backlight enable
  gpio_set_direction(10, GPIO_MODE_OUTPUT);
  gpio_set_level(10, 1);

  return dpi_panel;
}

uint32_t my_get_millis() { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel,
                                    esp_lcd_dpi_panel_event_data_t *edata,
                                    void *user_ctx) {
  lv_display_t *disp = (lv_display_t *)user_ctx;
  lv_display_flush_ready(disp);
  return false;
}
void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_buf) {
  /* Show the rendered image on the display */
  esp_lcd_panel_draw_bitmap(dpi_panel, area->x1, area->y1, area->x2 + 1,
                            area->y2 + 1, px_buf);
}

static void lvgl_task(void *arg) {
  while (1) {
    uint32_t time_till_next = lv_timer_handler();
    // TickType_t delay_ticks = pdMS_TO_TICKS(time_till_next);
    // if (delay_ticks == 0) {
    //   delay_ticks = 1;
    // }
    vTaskDelay(5);
  }
}

void display_init() {
  dpi_panel = init();

  lv_init();
  lv_tick_set_cb((lv_tick_get_cb_t)my_get_millis);

  lv_display_t *disp = lv_display_create(480, 640);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB888);
  lv_display_set_flush_cb(disp, my_flush_cb);

  esp_lcd_dpi_panel_event_callbacks_t cbs = {
      .on_color_trans_done = notify_lvgl_flush_ready,
  };

  // Note: Use the specific DSI/DPI registration function if available in your
  // IDF version
  esp_lcd_dpi_panel_register_event_callbacks(dpi_panel, &cbs, disp);

  size_t draw_buffer_sz = 480 * 128 * 3; // 1/10 screen size, 3 bytes per pixel
  void *buf1 = heap_caps_aligned_alloc(64, draw_buffer_sz, MALLOC_CAP_INTERNAL);
  if (!buf1) {
    buf1 = heap_caps_aligned_alloc(64, draw_buffer_sz, MALLOC_CAP_SPIRAM);
  }
  void *buf2 = heap_caps_aligned_alloc(64, draw_buffer_sz, MALLOC_CAP_INTERNAL);
  if (!buf2) {
    buf2 = heap_caps_aligned_alloc(64, draw_buffer_sz, MALLOC_CAP_SPIRAM);
  }

  lv_display_set_buffers(disp, buf1, buf2, draw_buffer_sz,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
}
