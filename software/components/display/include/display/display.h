#ifndef PEAK_DISPLAY_DISPLAY_H
#define PEAK_DISPLAY_DISPLAY_H

#include "esp_err.h"
#include <stdint.h>

enum {
  DISPLAY_STATE_SPEED = 1U << 0,
  DISPLAY_STATE_BATTERY_VOLTAGE = 1U << 1,
  DISPLAY_STATE_GEAR = 1U << 2,
  DISPLAY_STATE_SUPPORT_MODE = 1U << 3,
  DISPLAY_STATE_MOTOR_TEMP = 1U << 4,
  DISPLAY_STATE_CONTROLLER_TEMP = 1U << 5,
};

typedef enum {
  DISPLAY_SUPPORT_MODE_PAS = 0,
  DISPLAY_SUPPORT_MODE_TORQUE,
} display_support_mode_t;

/*
 * The display consumes a complete, latest-known state snapshot. Producers may
 * call display_update() from their task context; the display task is the only
 * task permitted to interact with LVGL.
 */
typedef struct {
  uint32_t valid_fields;
  float speed_kph;
  float battery_voltage_v;
  uint8_t gear;
  display_support_mode_t support_mode;
  int8_t motor_temp_c;
  int8_t controller_temp_c;
} display_state_t;

esp_err_t display_start(void);
esp_err_t display_update(const display_state_t *state);
/** Request one brief backlight-off/backlight-on blink. */
esp_err_t display_blink_backlight(void);
esp_err_t display_sleep(void);

#endif
