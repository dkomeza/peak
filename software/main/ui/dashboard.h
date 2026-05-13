#ifndef PEAK_UI_DASHBOARD_H
#define PEAK_UI_DASHBOARD_H

#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t speed_kmh;
  uint8_t battery_percent;
  uint16_t power_watts;
  uint8_t assist_segments;
  uint8_t estimated_range_km;
  float trip_distance_km;
  uint16_t ride_time_minutes;
  float average_speed_kmh;
  int8_t motor_temp_c;
  int8_t controller_temp_c;
  bool connected;
  const char *mode_label;
} peak_dashboard_data_t;

esp_err_t peak_dashboard_create(lv_obj_t *parent);
void peak_dashboard_update(const peak_dashboard_data_t *data);

#endif
