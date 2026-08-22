#ifndef PEAK_DISPLAY_EVENTS_H
#define PEAK_DISPLAY_EVENTS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  DISPLAY_EVENT_BOOT_STAGE,
  DISPLAY_EVENT_ACTION_RESULT,
  DISPLAY_EVENT_CONTROL_STATE,
  DISPLAY_EVENT_ESC_CONTROLLER_STATE,
  DISPLAY_EVENT_ESC_WALK_STATE,
  DISPLAY_EVENT_ESC_BATTERY,
  DISPLAY_EVENT_ESC_ENERGY,
  DISPLAY_EVENT_ESC_MOTOR,
  DISPLAY_EVENT_ESC_LIVE,
  DISPLAY_EVENT_ESC_TRIP_PRIMARY,
  DISPLAY_EVENT_ESC_TRIP_SECONDARY,
  DISPLAY_EVENT_LOCAL_BATTERY,
  DISPLAY_EVENT_AMBIENT,
  DISPLAY_EVENT_FAULT,
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
      uint8_t percent;
      float voltage_v;
      float current_a;
    } esc_battery;
    struct {
      float watt_hours;
      float amp_hours;
    } esc_energy;
    struct {
      int8_t motor_c;
      int8_t controller_c;
      float current_a;
      uint16_t rpm;
    } motor;
    struct {
      float speed_kph;
      uint16_t power_w;
    } live;
    struct {
      float distance_km;
      float elapsed_s;
      float average_kph;
      uint8_t range_km;
    } trip;
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
