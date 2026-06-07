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
#include "loom_benchmark.h"
#include <stdio.h>

static const char *TAG = "PEAK";
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 640
#define DISPLAY_SPEED_MAX_KPH 50
#define DISPLAY_ASSIST_MAX 6
#define DISPLAY_POWER_MAX_W 1200
#define DISPLAY_TEMP_MAX_C 100

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
  return (DISPLAY_WIDTH - text_width(font, text)) / 2;
}

static int right_aligned_x(const loom_font_t *font, const char *text,
                           int right) {
  return right - text_width(font, text);
}

static int centered_in_rect_x(const loom_font_t *font, const char *text,
                              loom_rect_t rect) {
  return rect.x + (rect.w - text_width(font, text)) / 2;
}

static int clamp_int(int value, int min, int max) {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

static uint8_t clamp_u8(uint8_t value, uint8_t max) {
  return value > max ? max : value;
}

static int percent_to_sweep(int value, int max, int sweep_max) {
  if (max <= 0) {
    return 0;
  }

  int clamped = clamp_int(value, 0, max);
  return (clamped * sweep_max) / max;
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

static loom_err_t display_draw_background(loom_t *gfx) {
  loom_err_t ret = loom_clear(gfx, loom_rgb(4, 6, 9));
  if (ret != LOOM_OK) {
    return ret;
  }

  loom_linear_gradient_t gradient = {
      .p0 = {0, 0},
      .p1 = {0, DISPLAY_HEIGHT},
      .color0 = loom_rgb(10, 18, 24),
      .color1 = loom_rgb(2, 5, 8),
  };
  return loom_fill_rect_linear_gradient(
      gfx, loom_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT), &gradient);
}

static loom_err_t display_draw_speed_gauge(loom_t *gfx,
                                           const esc_peak_data_t *data,
                                           const char *speed_text) {
  loom_point_t center = {DISPLAY_WIDTH / 2, 218};
  loom_stroke_t track = {
      .width = 16,
      .color = loom_rgba(255, 255, 255, 32),
  };
  loom_err_t ret = loom_draw_arc(gfx, center, 172, 150, 240, &track);

  int speed = (int)(data->speed + 0.5f);
  int sweep = percent_to_sweep(speed, DISPLAY_SPEED_MAX_KPH, 240);
  if (ret == LOOM_OK && sweep > 0) {
    loom_stroke_t stroke = {
        .width = 18,
        .color = loom_rgb(255, 255, 255),
    };
    loom_arc_gradient_t gradient = {
        .mode = LOOM_ARC_GRADIENT_SWEEP,
        .color0 = loom_rgb(86, 190, 255),
        .color1 = mode_color(data->ride_mode),
    };
    ret = loom_draw_arc_gradient(gfx, center, 172, 150, sweep, &stroke,
                                 &gradient);
  }

  loom_radial_gradient_t glow = {
      .center = center,
      .radius = 118,
      .color0 = loom_rgba(60, 130, 170, 58),
      .color1 = loom_rgba(0, 0, 0, 0),
  };
  if (ret == LOOM_OK) {
    ret = loom_fill_circle_radial_gradient(gfx, center, 118, &glow);
  }

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

  loom_text_style_t unit_style = {
      .color = loom_rgb(168, 190, 205),
      .opacity = 255,
      .size_px = 16,
  };
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_16, "KM/H",
                         centered_x(&loom_font_noto_sans_16, "KM/H"), 286,
                         &unit_style);
  }

  return ret;
}

static loom_err_t display_draw_assist_gauge(loom_t *gfx,
                                            const esc_peak_data_t *data,
                                            const char *gear_text) {
  loom_point_t center = {DISPLAY_WIDTH / 2, 382};
  loom_stroke_t outer = {
      .width = 3,
      .color = loom_rgba(230, 240, 246, 74),
  };
  loom_err_t ret = loom_stroke_circle(gfx, center, 78, &outer);

  loom_stroke_t track = {
      .width = 12,
      .color = loom_rgba(255, 255, 255, 28),
  };
  if (ret == LOOM_OK) {
    ret = loom_draw_arc(gfx, center, 64, 135, 270, &track);
  }

  int assist = clamp_u8(data->assist_level, DISPLAY_ASSIST_MAX);
  int sweep = percent_to_sweep(assist, DISPLAY_ASSIST_MAX, 270);
  if (ret == LOOM_OK && sweep > 0) {
    loom_stroke_t stroke = {
        .width = 12,
        .color = loom_rgb(255, 255, 255),
    };
    loom_arc_gradient_t gradient = {
        .mode = LOOM_ARC_GRADIENT_SWEEP,
        .color0 = loom_rgb(255, 207, 95),
        .color1 = mode_color(data->ride_mode),
    };
    ret = loom_draw_arc_gradient(gfx, center, 64, 135, sweep, &stroke,
                                 &gradient);
  }

  loom_radial_gradient_t fill = {
      .center = center,
      .radius = 56,
      .color0 = loom_rgba(255, 255, 255, 30),
      .color1 = loom_rgba(255, 255, 255, 4),
  };
  if (ret == LOOM_OK) {
    ret = loom_fill_circle_radial_gradient(gfx, center, 54, &fill);
  }

  loom_text_style_t gear_style = {
      .color = loom_rgb(245, 248, 250),
      .opacity = 255,
      .size_px = 96,
  };
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_digits_96, gear_text,
                         centered_x(&loom_font_noto_sans_digits_96,
                                    gear_text),
                         320, &gear_style);
  }

  loom_text_style_t support_style = {
      .color = mode_color(data->ride_mode),
      .opacity = 255,
      .size_px = 32,
  };
  if (ret == LOOM_OK) {
    const char *support_text = support_mode_text(data->support_mode);
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, support_text,
                         centered_x(&loom_font_noto_sans_32, support_text),
                         428, &support_style);
  }

  return ret;
}

static loom_err_t display_draw_status_pill(loom_t *gfx,
                                           const esc_peak_data_t *data,
                                           const char *voltage_text) {
  loom_rect_t pill = loom_rect(24, 22, 432, 54);
  loom_linear_gradient_t fill = {
      .p0 = {pill.x, pill.y},
      .p1 = {pill.x + pill.w, pill.y},
      .color0 = loom_rgba(20, 35, 42, 236),
      .color1 = loom_rgba(45, 58, 45, 236),
  };
  loom_err_t ret = loom_fill_round_rect_linear_gradient(gfx, pill, 18, &fill);

  loom_stroke_t border = {
      .width = 1,
      .color = loom_rgba(255, 255, 255, 36),
  };
  if (ret == LOOM_OK) {
    ret = loom_stroke_round_rect(gfx, pill, 18, &border);
  }

  uint8_t battery_pct = clamp_u8(data->battery_percentage, 100);
  loom_color_t indicator_color =
      battery_pct > 0 ? loom_rgb(85, 220, 135) : loom_rgb(255, 207, 95);
  loom_radial_gradient_t indicator = {
      .center = {50, 49},
      .radius = 17,
      .color0 = loom_rgb(255, 255, 255),
      .color1 = indicator_color,
  };
  if (ret == LOOM_OK) {
    ret = loom_fill_circle_radial_gradient(gfx, (loom_point_t){50, 49}, 15,
                                           &indicator);
  }

  char battery_text[8];
  const char *status_text = voltage_text;
  if (battery_pct > 0) {
    snprintf(battery_text, sizeof(battery_text), "%u%%",
             (unsigned)battery_pct);
    status_text = battery_text;
  }

  loom_text_style_t status_style = {
      .color = loom_rgb(238, 246, 248),
      .opacity = 255,
      .size_px = 32,
  };
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, status_text, 78, 32,
                         &status_style);
  }

  loom_text_style_t voltage_style = {
      .color = loom_rgb(170, 192, 202),
      .opacity = 255,
      .size_px = 16,
  };
  if (ret == LOOM_OK && battery_pct > 0) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_16, voltage_text, 158, 48,
                         &voltage_style);
  }

  loom_text_style_t mode_style = {
      .color = mode_color(data->ride_mode),
      .opacity = 255,
      .size_px = 32,
  };
  if (ret == LOOM_OK) {
    const char *mode_text =
        data->ride_mode == CYCLEIQ_RIDE_MODE_MOUNTAIN ? "MTN" : "ROAD";
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, mode_text,
                         right_aligned_x(&loom_font_noto_sans_32, mode_text,
                                         pill.x + pill.w - 18),
                         32, &mode_style);
  }

  return ret;
}

static loom_err_t display_draw_metric_panel(loom_t *gfx, loom_rect_t rect,
                                            const char *label,
                                            const char *value,
                                            loom_color_t accent) {
  loom_linear_gradient_t fill = {
      .p0 = {rect.x, rect.y},
      .p1 = {rect.x + rect.w, rect.y + rect.h},
      .color0 = loom_rgba(18, 27, 33, 232),
      .color1 = loom_rgba(accent.r, accent.g, accent.b, 52),
  };
  loom_err_t ret = loom_fill_round_rect_linear_gradient(gfx, rect, 14, &fill);

  loom_text_style_t label_style = {
      .color = loom_rgb(132, 152, 164),
      .opacity = 255,
      .size_px = 16,
  };
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_16, label, rect.x + 14,
                         rect.y + 10, &label_style);
  }

  loom_text_style_t value_style = {
      .color = loom_rgb(232, 240, 244),
      .opacity = 255,
      .size_px = 32,
  };
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, value,
                         centered_in_rect_x(&loom_font_noto_sans_32, value,
                                            rect),
                         rect.y + 32, &value_style);
  }

  loom_radial_gradient_t dot = {
      .center = {rect.x + rect.w - 17, rect.y + 18},
      .radius = 9,
      .color0 = loom_rgb(255, 255, 255),
      .color1 = accent,
  };
  if (ret == LOOM_OK) {
    ret = loom_fill_circle_radial_gradient(
        gfx, (loom_point_t){rect.x + rect.w - 17, rect.y + 18}, 8, &dot);
  }

  return ret;
}

static loom_err_t display_draw_telemetry_row(loom_t *gfx,
                                             const esc_peak_data_t *data,
                                             const char *motor_temp_text,
                                             const char *controller_temp_text,
                                             const char *power_text) {
  int power_sweep = percent_to_sweep(data->power, DISPLAY_POWER_MAX_W, 120);
  loom_color_t power_color = power_sweep > 80 ? loom_rgb(255, 207, 95)
                                              : loom_rgb(104, 202, 255);
  int temp = data->motor_temperature > data->controller_temperature
                 ? data->motor_temperature
                 : data->controller_temperature;
  int temp_sweep = percent_to_sweep(temp, DISPLAY_TEMP_MAX_C, 120);
  loom_color_t temp_color = temp_sweep > 80 ? loom_rgb(255, 94, 94)
                                            : loom_rgb(130, 220, 180);

  loom_err_t ret =
      display_draw_metric_panel(gfx, loom_rect(24, 536, 132, 78), "MOTOR",
                                motor_temp_text, temp_color);
  if (ret == LOOM_OK) {
    ret = display_draw_metric_panel(gfx, loom_rect(174, 536, 132, 78), "CTRL",
                                    controller_temp_text, temp_color);
  }
  if (ret == LOOM_OK) {
    ret = display_draw_metric_panel(gfx, loom_rect(324, 536, 132, 78), "POWER",
                                    power_text, power_color);
  }

  return ret;
}

static loom_err_t display_draw_button_toast(loom_t *gfx,
                                            display_button_event_t event,
                                            bool error) {
  const char *event_text = button_event_text(event, error);
  loom_rect_t toast = loom_rect(126, 88, 228, 46);
  loom_color_t accent =
      error ? loom_rgb(255, 86, 86) : loom_rgb(255, 211, 80);
  loom_linear_gradient_t fill = {
      .p0 = {toast.x, toast.y},
      .p1 = {toast.x + toast.w, toast.y},
      .color0 = loom_rgba(18, 22, 27, 244),
      .color1 = loom_rgba(accent.r, accent.g, accent.b, 96),
  };
  loom_err_t ret =
      loom_fill_round_rect_linear_gradient(gfx, toast, 16, &fill);

  loom_stroke_t stroke = {
      .width = 1,
      .color = loom_rgba(accent.r, accent.g, accent.b, 128),
  };
  if (ret == LOOM_OK) {
    ret = loom_stroke_round_rect(gfx, toast, 16, &stroke);
  }

  loom_text_style_t button_style = {
      .color = accent,
      .opacity = 255,
      .size_px = 32,
  };
  if (ret == LOOM_OK) {
    ret = loom_draw_text(gfx, &loom_font_noto_sans_32, event_text,
                         centered_in_rect_x(&loom_font_noto_sans_32,
                                            event_text, toast),
                         96, &button_style);
  }

  return ret;
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
        .width = DISPLAY_WIDTH,
        .height = DISPLAY_HEIGHT,
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
  snprintf(gear_text, sizeof(gear_text), "%u",
           (unsigned)clamp_u8(data.assist_level, DISPLAY_ASSIST_MAX));
  snprintf(voltage_text, sizeof(voltage_text), "%.1fV", data.battery_voltage);
  snprintf(motor_temp_text, sizeof(motor_temp_text), "M:%dC",
           data.motor_temperature);
  snprintf(controller_temp_text, sizeof(controller_temp_text), "C:%dC",
           data.controller_temperature);
  snprintf(power_text, sizeof(power_text), "%uW", (unsigned)data.power);

  ret = display_draw_background(gfx);
  if (ret == LOOM_OK) {
    ret = display_draw_speed_gauge(gfx, &data, speed_text);
  }
  if (ret == LOOM_OK) {
    ret = display_draw_assist_gauge(gfx, &data, gear_text);
  }
  if (ret == LOOM_OK) {
    ret = display_draw_telemetry_row(gfx, &data, motor_temp_text,
                                     controller_temp_text, power_text);
  }
  if (ret == LOOM_OK) {
    ret = display_draw_status_pill(gfx, &data, voltage_text);
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
    ret = display_draw_button_toast(gfx, button_event, button_event_error);
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

#if CONFIG_PEAK_LOOM_BENCHMARK
  ESP_RETURN_ON_ERROR(loom_benchmark_run(dpi_panel), TAG,
                      "run loom benchmark");
#endif

  xTaskCreate(display_task, "display_task", 4096, NULL, 5, NULL);

  return ESP_OK;
}
