#ifndef PEAK_DISPLAY_EVENTS_H
#define PEAK_DISPLAY_EVENTS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  DISPLAY_EVENT_BOOT_STAGE,
  DISPLAY_EVENT_ACTION_RESULT,
  DISPLAY_EVENT_CONTROL_STATE,
  DISPLAY_EVENT_ESC_STATE,
  DISPLAY_EVENT_LOCAL_BATTERY,
  DISPLAY_EVENT_AMBIENT,
  DISPLAY_EVENT_FAULT,
  DISPLAY_EVENT_SLEEP,
} display_event_type_t;

typedef enum {
  DISPLAY_ACTION_GEAR,
  DISPLAY_ACTION_SUPPORT_MODE,
  DISPLAY_ACTION_RIDE_MODE,
  DISPLAY_ACTION_WALK_MODE,
  DISPLAY_ACTION_POWER,
} display_action_t;

typedef enum {
  DISPLAY_FAULT_DISPLAY,
  DISPLAY_FAULT_ESC,
  DISPLAY_FAULT_CAN,
  DISPLAY_FAULT_SENSOR,
} display_fault_source_t;

enum {
  DISPLAY_ESC_STATE_GEAR = 1U << 0,
  DISPLAY_ESC_STATE_SUPPORT_MODE = 1U << 1,
  DISPLAY_ESC_STATE_RIDE_MODE = 1U << 2,
  DISPLAY_ESC_STATE_WALK = 1U << 3,
  DISPLAY_ESC_STATE_SPEED = 1U << 4,
  DISPLAY_ESC_STATE_POWER = 1U << 5,
  DISPLAY_ESC_STATE_MOTOR_TEMP = 1U << 6,
  DISPLAY_ESC_STATE_CONTROLLER_TEMP = 1U << 7,
  DISPLAY_ESC_STATE_BATTERY_PERCENT = 1U << 8,
  DISPLAY_ESC_STATE_BATTERY_VOLTAGE = 1U << 9,
};

typedef struct {
  display_event_type_t type;
  uint32_t timestamp_ms;
  union {
    struct {
      uint8_t stage;
    } boot;
    struct {
      display_action_t action;
      esp_err_t result;
    } action;
    struct {
      uint8_t gear;
      uint8_t support_mode;
      uint8_t ride_mode;
      bool walk_active;
    } control;
    struct {
      uint32_t valid_fields;
      uint8_t gear;
      uint8_t support_mode;
      uint8_t ride_mode;
      bool walk_active;
      float speed_kph;
      int16_t power_w;
      int8_t motor_temp_c;
      int8_t controller_temp_c;
      uint8_t battery_percent;
      float battery_voltage_v;
    } esc_state;
    struct {
      float voltage_v;
    } local_battery;
    struct {
      float lux;
      float board_temp_c;
    } ambient;
    struct {
      display_fault_source_t source;
      esp_err_t code;
    } fault;
  } data;
} display_event_t;

esp_err_t display_event_publish(const display_event_t *event);

#endif
