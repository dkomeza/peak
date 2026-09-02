#ifndef ESC_ESC_H
#define ESC_ESC_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  ESC_SUPPORT_MODE_PAS = 0,
  ESC_SUPPORT_MODE_TORQUE,
} esc_support_mode_t;

typedef enum {
  ESC_RIDE_MODE_NORMAL = 0,
  ESC_RIDE_MODE_MOUNTAIN,
} esc_ride_mode_t;

enum {
  ESC_STATE_GEAR = 1u << 0,
  ESC_STATE_SUPPORT_MODE = 1u << 1,
  ESC_STATE_RIDE_MODE = 1u << 2,
  ESC_STATE_WALK = 1u << 3,
  ESC_STATE_SPEED = 1u << 4,
  ESC_STATE_POWER = 1u << 5,
  ESC_STATE_MOTOR_TEMP = 1u << 6,
  ESC_STATE_CONTROLLER_TEMP = 1u << 7,
  ESC_STATE_BATTERY_PERCENT = 1u << 8,
  ESC_STATE_BATTERY_VOLTAGE = 1u << 9,
};

typedef struct {
  uint32_t valid_fields;
  uint8_t gear;
  esc_support_mode_t support_mode;
  esc_ride_mode_t ride_mode;
  bool walk_active;
  float speed_kph;
  int16_t power_w;
  int8_t motor_temp_c;
  int8_t controller_temp_c;
  uint8_t battery_percent;
  float battery_voltage_v;
} esc_state_t;

typedef void (*esc_update_cb_t)(const esc_state_t *state, void *user_ctx);

esp_err_t esc_set_update_callback(esc_update_cb_t callback, void *user_ctx);
esp_err_t esc_init(void);
void esc_get_state(esc_state_t *out);

esp_err_t esc_gear_up(void);
esp_err_t esc_gear_down(void);
esp_err_t esc_set_support_mode(esc_support_mode_t mode);
esp_err_t esc_set_ride_mode(esc_ride_mode_t mode);
esp_err_t esc_set_walk(bool enabled);

#endif
