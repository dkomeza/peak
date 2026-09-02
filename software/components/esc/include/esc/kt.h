#ifndef ESC_KT_H
#define ESC_KT_H

#include <stdbool.h>
#include <stdint.h>

#include "esc/esc.h"

typedef struct {
  uint8_t wheel_code;
  uint16_t wheel_circumference_mm;
  uint8_t max_speed_kph;
  bool light;
  uint8_t p1;
  uint8_t p2;
  uint8_t p3;
  uint8_t p4;
  uint8_t p5;
  uint8_t c1;
  uint8_t c2;
  uint8_t c4;
  uint8_t c5;
  uint8_t c12;
  uint8_t c13;
  uint8_t c14;
} esc_kt_settings_t;

typedef struct {
  uint8_t battery_percent;
  float speed_kph;
  float wheel_rpm;
  uint16_t power_w;
  int8_t motor_temp_c;
  bool throttle_active;
  bool cruise_active;
  bool assist_active;
  bool brake_active;
  uint8_t gear;
  esc_ride_mode_t ride_mode;
  bool walk_active;
  bool has_telemetry;
} esc_kt_snapshot_t;

typedef void (*esc_kt_update_cb_t)(const esc_kt_snapshot_t *snapshot,
                                   void *context);

esp_err_t esc_kt_set_update_callback(esc_kt_update_cb_t callback,
                                      void *context);
esp_err_t esc_kt_init(void);
esp_err_t esc_kt_set_settings(const esc_kt_settings_t *settings);
esp_err_t esc_kt_gear_up(void);
esp_err_t esc_kt_gear_down(void);
esp_err_t esc_kt_set_ride_mode(esc_ride_mode_t mode);
esp_err_t esc_kt_set_walk(bool enabled);
esp_err_t esc_kt_get_snapshot(esc_kt_snapshot_t *out);

#endif
