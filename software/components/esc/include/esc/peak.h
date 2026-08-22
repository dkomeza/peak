#ifndef ESC_PEAK_H
#define ESC_PEAK_H

#include "cycleiq_protocol.h"
#include "esc/controller.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  // Battery data
  uint8_t battery_percentage;
  float battery_voltage;
  float battery_current;
  float watt_hours;
  float amp_hours;

  // Motor data
  int8_t motor_temperature;
  int8_t controller_temperature;
  float motor_current;
  uint16_t motor_rpm;

  // Controller data
  uint8_t assist_level;
  cycleiq_support_mode_t support_mode;
  cycleiq_ride_mode_t ride_mode;
  bool walk_active;

  // Live data
  float speed;
  uint16_t power;

  // Trip data
  float trip_distance;
  float trip_time;
  float trip_average_speed;
  uint8_t trip_estimated_range;
} esc_peak_data_t;

typedef enum {
  ESC_PEAK_UPDATE_BATTERY_STATUS,
  ESC_PEAK_UPDATE_BATTERY_ENERGY,
  ESC_PEAK_UPDATE_MOTOR_STATUS,
  ESC_PEAK_UPDATE_CONTROLLER_STATE,
  ESC_PEAK_UPDATE_LIVE_STATUS,
  ESC_PEAK_UPDATE_TRIP_PRIMARY,
  ESC_PEAK_UPDATE_TRIP_SECONDARY,
  ESC_PEAK_UPDATE_WALK_STATE,
} esc_peak_update_type_t;

typedef struct {
  esc_peak_update_type_t type;
  union {
    struct {
      uint8_t percentage;
      float voltage_v;
      float current_a;
    } battery;
    struct {
      float watt_hours;
      float amp_hours;
    } energy;
    struct {
      int8_t motor_c;
      int8_t controller_c;
      float current_a;
      uint16_t rpm;
    } motor;
    struct {
      uint8_t assist_level;
      uint8_t support_mode;
      uint8_t ride_mode;
    } controller;
    struct {
      float speed_kph;
      uint16_t power_w;
    } live;
    struct {
      float distance_km;
      float time_s;
    } trip_primary;
    struct {
      float average_speed_kph;
      uint8_t estimated_range_km;
    } trip_secondary;
    struct {
      bool active;
    } walk;
  } data;
} esc_peak_update_t;

typedef void (*esc_peak_update_cb_t)(const esc_peak_update_t *update,
                                     void *user_ctx);

/**
 * Initializes the ESC Peak module.
 */
void esc_peak_init(void);

/** Register one boot-time observer for decoded PEAK telemetry groups. */
esp_err_t esc_peak_set_update_callback(esc_peak_update_cb_t callback,
                                       void *user_ctx);

/**
 * Initializes a caller-owned PEAK controller command handle.
 */
esp_err_t esc_peak_controller_init(esc_controller_t *out);

/**
 * Gets the latest data received from the ESC.
 * This is thread safe and blocking.
 */
void esc_peak_get_data(esc_peak_data_t *data);

#endif
