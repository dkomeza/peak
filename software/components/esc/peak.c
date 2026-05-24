#include "esc/peak.h"
#include "peak_private.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "connection/can.h"
#include "esp_err.h"
#include <stdbool.h>

static SemaphoreHandle_t esc_peak_data_mutex;

static esc_peak_data_t esc_peak_data = {0};

static bool esc_peak_ensure_data_mutex(void) {
  if (esc_peak_data_mutex == NULL) {
    esc_peak_data_mutex = xSemaphoreCreateMutex();
  }

  return esc_peak_data_mutex != NULL;
}

static uint32_t esc_peak_packet_base_id(void) {
  return (uint32_t)PEAK_CAN_ID << 8;
}

static uint32_t esc_peak_command_id(cycleiq_command_t command) {
  return esc_peak_packet_base_id() | command;
}

static esp_err_t esc_peak_send_command(cycleiq_command_t command,
                                       const uint8_t *data, uint8_t len) {
  return can_send(esc_peak_command_id(command), data, len, 0);
}

static esp_err_t esc_peak_set_power(void *ctx, bool enabled) {
  (void)ctx;

  return esc_peak_send_command(enabled ? CYCLEIQ_POWER_ON : CYCLEIQ_POWER_OFF,
                               NULL, 0);
}

static esp_err_t esc_peak_set_ride_mode(void *ctx, esc_ride_mode_t mode) {
  (void)ctx;

  uint8_t payload = (uint8_t)mode;
  return esc_peak_send_command(CYCLEIQ_COMM_RIDE_MODE_SET, &payload,
                               sizeof(payload));
}

static esp_err_t esc_peak_set_gear(void *ctx, uint8_t gear) {
  (void)ctx;

  return esc_peak_send_command(CYCLEIQ_COMM_GEAR_SET, &gear, sizeof(gear));
}

static esp_err_t esc_peak_set_support_mode(void *ctx, esc_support_mode_t mode) {
  (void)ctx;

  uint8_t payload = (uint8_t)mode;
  return esc_peak_send_command(CYCLEIQ_COMM_MODE_SET, &payload,
                               sizeof(payload));
}

static const esc_controller_ops_t s_peak_controller_ops = {
    .name = "PEAK",
    .set_power = esc_peak_set_power,
    .set_ride_mode = esc_peak_set_ride_mode,
    .set_gear = esc_peak_set_gear,
    .set_support_mode = esc_peak_set_support_mode,
};

static uint16_t read_be_u16(const uint8_t *data) {
  return ((uint16_t)data[0] << 8) | data[1];
}

static int16_t read_be_i16(const uint8_t *data) {
  return (int16_t)read_be_u16(data);
}

static uint32_t read_be_u32(const uint8_t *data) {
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | data[3];
}

static void esc_peak_parse_battery_status(const uint8_t *data, uint8_t len) {
  if (len != 5) {
    return;
  }

  esc_peak_data.battery_percentage = data[0];
  esc_peak_data.battery_voltage = read_be_u16(data + 1) / 100.0f;
  esc_peak_data.battery_current = read_be_i16(data + 3) / 100.0f;
}

static void esc_peak_parse_battery_energy(const uint8_t *data, uint8_t len) {
  if (len != 4) {
    return;
  }

  esc_peak_data.watt_hours = read_be_u16(data) / 10.0f;
  esc_peak_data.amp_hours = read_be_u16(data + 2) / 100.0f;
}

static void esc_peak_parse_motor_status(const uint8_t *data, uint8_t len) {
  if (len != 6) {
    return;
  }

  esc_peak_data.motor_temperature = (int8_t)data[0];
  esc_peak_data.controller_temperature = (int8_t)data[1];
  esc_peak_data.motor_current = read_be_i16(data + 2) / 100.0f;
  esc_peak_data.motor_rpm = read_be_u16(data + 4);
}

static void esc_peak_parse_controller_state(const uint8_t *data, uint8_t len) {
  if (len != 3) {
    return;
  }

  esc_peak_data.assist_level = data[0];
  esc_peak_data.support_mode = (cycleiq_support_mode_t)data[1];
  esc_peak_data.ride_mode = (cycleiq_ride_mode_t)data[2];
}

static void esc_peak_parse_live_status(const uint8_t *data, uint8_t len) {
  if (len != 4) {
    return;
  }

  esc_peak_data.speed = read_be_u16(data) / 100.0f;
  esc_peak_data.power = read_be_u16(data + 2);
}

static void esc_peak_parse_trip_primary(const uint8_t *data, uint8_t len) {
  if (len != 8) {
    return;
  }

  esc_peak_data.trip_distance = read_be_u32(data) / 1000.0f;
  esc_peak_data.trip_time = (float)read_be_u32(data + 4);
}

static void esc_peak_parse_trip_secondary(const uint8_t *data, uint8_t len) {
  if (len != 3) {
    return;
  }

  esc_peak_data.trip_average_speed = read_be_u16(data) / 100.0f;
  esc_peak_data.trip_estimated_range = data[2];
}

void esc_peak_parse_data(uint32_t id, const uint8_t *data, uint8_t len,
                         void *user_data) {
  (void)user_data;

  if (!esc_peak_ensure_data_mutex()) {
    return;
  }

  xSemaphoreTake(esc_peak_data_mutex, portMAX_DELAY);

  switch ((peak_packet_type_t)(id & 0xFF)) {
  case PEAK_PACKET_TYPE_BATTERY_STATUS:
    esc_peak_parse_battery_status(data, len);
    break;
  case PEAK_PACKET_TYPE_BATTERY_ENERGY:
    esc_peak_parse_battery_energy(data, len);
    break;
  case PEAK_PACKET_TYPE_MOTOR_STATUS:
    esc_peak_parse_motor_status(data, len);
    break;
  case PEAK_PACKET_TYPE_CONTROLLER_STATE:
    esc_peak_parse_controller_state(data, len);
    break;
  case PEAK_PACKET_TYPE_LIVE_STATUS:
    esc_peak_parse_live_status(data, len);
    break;
  case PEAK_PACKET_TYPE_TRIP_PRIMARY:
    esc_peak_parse_trip_primary(data, len);
    break;
  case PEAK_PACKET_TYPE_TRIP_SECONDARY:
    esc_peak_parse_trip_secondary(data, len);
    break;
  default:
    break;
  }

  xSemaphoreGive(esc_peak_data_mutex);
}

void esc_peak_init(void) {
  esc_peak_ensure_data_mutex();

  can_register_cb(CYCLEIQ_CAN_ID << 8, 0xFF00, esc_peak_parse_data, NULL);
}

esp_err_t esc_peak_controller_init(esc_controller_t *out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  out->ops = &s_peak_controller_ops;
  out->ctx = NULL;
  return ESP_OK;
}

void esc_peak_get_data(esc_peak_data_t *data) {
  if (data == NULL) {
    return;
  }

  if (!esc_peak_ensure_data_mutex()) {
    *data = esc_peak_data;
    return;
  }

  xSemaphoreTake(esc_peak_data_mutex, portMAX_DELAY);
  *data = esc_peak_data;
  xSemaphoreGive(esc_peak_data_mutex);
}
