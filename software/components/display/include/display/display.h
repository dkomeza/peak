#ifndef PEAK_DISPLAY_DISPLAY_H
#define PEAK_DISPLAY_DISPLAY_H

#include "esp_err.h"
#include <stdint.h>

enum {
  DISPLAY_STATE_SPEED = 1U << 0,
  DISPLAY_STATE_BATTERY_VOLTAGE = 1U << 1,
  DISPLAY_STATE_GEAR = 1U << 2,
};

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
} display_state_t;

esp_err_t display_start(void);
esp_err_t display_update(const display_state_t *state);
esp_err_t display_sleep(void);

#endif
