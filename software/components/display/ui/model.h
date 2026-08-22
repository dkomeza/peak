#ifndef PEAK_DISPLAY_UI_MODEL_H
#define PEAK_DISPLAY_UI_MODEL_H

#include "display/events.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  float speed_kph;
  uint16_t power_w;
  uint8_t battery_percent;
  float battery_voltage_v;
  uint8_t gear;
  uint8_t support_mode;
  uint8_t ride_mode;
  bool walk_active;
  bool has_live_data;
  bool has_battery_data;
  bool has_control_data;
} display_ui_model_t;

bool display_ui_model_apply(display_ui_model_t *model,
                            const display_event_t *event);

#endif
