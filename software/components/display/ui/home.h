#ifndef PEAK_DISPLAY_HOME_H
#define PEAK_DISPLAY_HOME_H

#include <stdbool.h>
#include <stdint.h>

#include "display/display.h"
#include "lvgl.h"

#define DISPLAY_HOME_GEAR_COUNT 5

/*
 * Home-screen-owned LVGL objects and their rendered values. The display task
 * owns this context and is the only task allowed to call these functions.
 */
typedef struct {
  lv_obj_t *screen;
  lv_obj_t *support_mode;
  lv_obj_t *speed;
  lv_obj_t *battery_voltage;
  lv_obj_t *gear[DISPLAY_HOME_GEAR_COUNT];

  bool support_mode_valid;
  display_support_mode_t support_mode_display;
  bool speed_valid;
  int32_t speed_display_kph;
  bool battery_voltage_valid;
  int32_t battery_voltage_tenths;
  bool gear_valid;
  uint8_t gear_display;
} display_home_t;

void display_home_create(display_home_t *home);
void display_home_update(display_home_t *home, const display_state_t *state);

#endif
