#include "display.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esc/peak.h"
#include "freertos/idf_additions.h"
#include "loom/fonts.h"
#include "loom/loom.h"
#include "loom/loom_esp_idf.h"
#include <stdio.h>

static const char *TAG = "PEAK";
static portMUX_TYPE s_button_event_lock = portMUX_INITIALIZER_UNLOCKED;
static display_button_event_t s_button_event = DISPLAY_BUTTON_EVENT_NONE;
static bool s_button_event_error = false;
static uint32_t s_button_event_until_ms = 0;

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

static int text_width(const loom_font_t *font, const char *text) {
  if (font == NULL || text == NULL) {
    return 0;
  }

  int width = 0;
  for (const char *p = text; *p != '\0'; p++) {
    bool found = false;
    for (uint16_t i = 0; i < font->glyph_count; i++) {
      if (font->glyphs[i].codepoint == (uint8_t)*p) {
        width += font->glyphs[i].advance_x;
        found = true;
        break;
      }
    }

    if (!found) {
      width += font->line_height > 0 ? font->line_height / 2 : 8;
    }
  }

  return width;
}

static int centered_x(const loom_font_t *font, const char *text) {
  return (480 - text_width(font, text)) / 2;
}

static int right_aligned_x(const loom_font_t *font, const char *text,
                           int right) {
  return right - text_width(font, text);
}

static const char *support_mode_text(cycleiq_support_mode_t mode) {
  return mode == CYCLEIQ_MODE_TORQUE ? "TQ" : "PAS";
}

static loom_color_t mode_color(cycleiq_ride_mode_t mode) {
  return mode == CYCLEIQ_RIDE_MODE_MOUNTAIN ? loom_rgb(230, 42, 42)
                                            : loom_rgb(120, 210, 255);
}

static const char *button_event_text(display_button_event_t event,
                                     bool error) {
  switch (event) {
  case DISPLAY_BUTTON_EVENT_UP:
    return error ? "UP ERR" : "UP";
  case DISPLAY_BUTTON_EVENT_POWER:
    return error ? "PWR ERR" : "PWR";
  case DISPLAY_BUTTON_EVENT_DOWN:
    return error ? "DOWN ERR" : "DOWN";
  default:
    return "";
  }
}

void display_show_button_event(display_button_event_t event, bool error) {
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

  taskENTER_CRITICAL(&s_button_event_lock);
  s_button_event = event;
  s_button_event_error = error;
  s_button_event_until_ms = now_ms + 2000;
  taskEXIT_CRITICAL(&s_button_event_lock);
}

esp_lcd_panel_handle_t init(void) {
  // LDO Power
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
  esp_lcd_dpi_panel_config_t dpi_cfg = {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = 20,
      .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888,
      .video_timing =
          {
              .h_size = 480,
              .v_size = 640,
              .hsync_pulse_width = 2,
              .hsync_back_porch = 6,
              .hsync_front_porch = 14,
              .vsync_pulse_width = 2,
              .vsync_back_porch = 12,
              .vsync_front_porch = 8,
          },
      .flags.use_dma2d = 1,
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
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
      .bits_per_pixel = 24,
      .vendor_config = &vendor_cfg,
  };

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

static esp_err_t display_demo(void) {
  static loom_t *gfx = NULL;
  static loom_esp_idf_t *gfx_backend = NULL;
  if (gfx == NULL) {
    loom_esp_idf_config_t cfg = {
        .width = 480,
        .height = 640,
        .format = LOOM_PIXEL_FORMAT_RGB888,
        .tile_height = 64,
        .buffer_count = 2,
        .command_capacity = 128,
        .panel = dpi_panel,
    };

    ESP_RETURN_ON_ERROR(loom_esp_idf_create(&cfg, &gfx_backend, &gfx), TAG,
                        "create loom");
  }

  loom_err_t ret = loom_begin_frame(gfx);
  if (ret != LOOM_OK) {
    return loom_err_to_esp_err(ret);
  }

  esc_peak_data_t data;
  esc_peak_get_data(&data);

  char speed_text[8];
  char gear_text[4];
  char voltage_text[16];
  char motor_temp_text[16];
  char controller_temp_text[16];
  char power_text[16];

  int speed = (int)(data.speed + 0.5f);
  snprintf(speed_text, sizeof(speed_text), "%d", speed);
  snprintf(gear_text, sizeof(gear_text), "%u", data.assist_level);
  snprintf(voltage_text, sizeof(voltage_text), "%.1fV", data.battery_voltage);
  snprintf(motor_temp_text, sizeof(motor_temp_text), "M:%dC",
           data.motor_temperature);
  snprintf(controller_temp_text, sizeof(controller_temp_text), "C:%dC",
           data.controller_temperature);
  snprintf(power_text, sizeof(power_text), "%uW", data.power);

  ret = loom_clear(gfx, loom_rgb(5, 7, 9));

  loom_text_style_t speed_style = {
      .color = loom_rgb(255, 255, 255),
      .opacity = 255,
      .size_px = 144,
  };
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_digits_144, speed_text,
                         centered_x(&loom_font_noto_sans_digits_144,
                                    speed_text),
                         150, &speed_style);
  }

  loom_text_style_t gear_style = {
      .color = loom_rgb(245, 248, 250),
      .opacity = 255,
      .size_px = 96,
  };
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_digits_96, gear_text,
                         centered_x(&loom_font_noto_sans_digits_96, gear_text),
                         310, &gear_style);
  }

  loom_text_style_t support_style = {
      .color = mode_color(data.ride_mode),
      .opacity = 255,
      .size_px = 32,
  };
  if (ret == LOOM_OK) {
    const char *support_text = support_mode_text(data.support_mode);
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, support_text,
                         centered_x(&loom_font_noto_sans_32, support_text),
                         410, &support_style);
  }

  loom_text_style_t small_style = {
      .color = loom_rgb(220, 230, 238),
      .opacity = 255,
      .size_px = 32,
  };
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, voltage_text,
                         right_aligned_x(&loom_font_noto_sans_32, voltage_text,
                                         456),
                         28, &small_style);
  }
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, motor_temp_text, 24, 552,
                         &small_style);
  }
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, controller_temp_text, 24,
                         586, &small_style);
  }
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, power_text,
                         right_aligned_x(&loom_font_noto_sans_32, power_text,
                                         456),
                         586, &small_style);
  }

  display_button_event_t button_event;
  bool button_event_error;
  uint32_t button_event_until_ms;
  taskENTER_CRITICAL(&s_button_event_lock);
  button_event = s_button_event;
  button_event_error = s_button_event_error;
  button_event_until_ms = s_button_event_until_ms;
  taskEXIT_CRITICAL(&s_button_event_lock);

  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  if (ret == LOOM_OK && button_event != DISPLAY_BUTTON_EVENT_NONE &&
      now_ms < button_event_until_ms) {
    const char *event_text = button_event_text(button_event, button_event_error);
    loom_text_style_t button_style = {
        .color = button_event_error ? loom_rgb(255, 86, 86)
                                    : loom_rgb(255, 211, 80),
        .opacity = 255,
        .size_px = 32,
    };
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, event_text,
                         centered_x(&loom_font_noto_sans_32, event_text), 92,
                         &button_style);
  }

  loom_err_t end_ret = loom_end_frame(gfx);
  return loom_err_to_esp_err(ret != LOOM_OK ? ret : end_ret);
}

void display_task(void *arg) {

  for (;;) {
    display_demo();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

esp_err_t display_init(void) {
  dpi_panel = init();

  xTaskCreate(display_task, "display_task", 4096, NULL, 5, NULL);

  return ESP_OK;
}
