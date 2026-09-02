#ifndef PEAK_DISPLAY_UI_MODEL_H
#define PEAK_DISPLAY_UI_MODEL_H

#include "display/events.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  float speed_kph;
  int16_t power_w;
  uint8_t battery_percent;
  float battery_voltage_v;
  uint8_t gear;
  uint8_t support_mode;
  uint8_t ride_mode;
  bool walk_active;
  int8_t motor_temp_c;
  int8_t controller_temp_c;
  bool has_speed;
  bool has_power;
  bool has_battery_percent;
  bool has_battery_voltage;
  bool has_gear;
  bool has_support_mode;
  bool has_ride_mode;
  bool has_motor_temp;
  bool has_controller_temp;
} display_ui_model_t;

bool display_ui_model_apply(display_ui_model_t *model,
                            const display_event_t *event);

#endif
