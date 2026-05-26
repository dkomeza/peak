#include "esc/peak.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "connection/can.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "esc_peak";

static SemaphoreHandle_t esc_peak_data_mutex;

static esc_peak_data_t esc_peak_data = {0};

static bool esc_peak_ensure_data_mutex(void) {
  if (esc_peak_data_mutex == NULL) {
    esc_peak_data_mutex = xSemaphoreCreateMutex();
  }

  return esc_peak_data_mutex != NULL;
}

static esp_err_t esc_peak_send_frame(const cycleiq_frame_t *frame) {
  if (frame == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  return can_send(frame->id, frame->data, frame->len, 0);
}

static esp_err_t esc_peak_send_built_frame(bool built,
                                           const cycleiq_frame_t *frame) {
  if (!built) {
    return ESP_ERR_INVALID_ARG;
  }

  return esc_peak_send_frame(frame);
}

static esp_err_t esc_peak_request_protocol_version(void) {
  cycleiq_frame_t frame;
  return esc_peak_send_built_frame(cycleiq_get_protocol_version(&frame),
                                   &frame);
}

static esp_err_t esc_peak_set_power(void *ctx, bool enabled) {
  (void)ctx;

  cycleiq_frame_t frame;
  bool built = enabled ? cycleiq_power_on(&frame) : cycleiq_power_off(&frame);
  return esc_peak_send_built_frame(built, &frame);
}

static esp_err_t esc_peak_set_ride_mode(void *ctx, esc_ride_mode_t mode) {
  (void)ctx;

  cycleiq_frame_t frame;
  return esc_peak_send_built_frame(
      cycleiq_set_ride_mode(&frame, (cycleiq_ride_mode_t)mode), &frame);
}

static esp_err_t esc_peak_set_gear(void *ctx, uint8_t gear) {
  (void)ctx;

  cycleiq_frame_t frame;
  return esc_peak_send_built_frame(cycleiq_set_gear(&frame, gear), &frame);
}

static esp_err_t esc_peak_set_support_mode(void *ctx, esc_support_mode_t mode) {
  (void)ctx;

  cycleiq_frame_t frame;
  return esc_peak_send_built_frame(
      cycleiq_set_support_mode(&frame, (cycleiq_support_mode_t)mode), &frame);
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
  if (len != PEAK_PACKET_BATTERY_STATUS_LEN) {
    return;
  }

  esc_peak_data.battery_percentage = data[0];
  esc_peak_data.battery_voltage = read_be_u16(data + 1) / 100.0f;
  esc_peak_data.battery_current = read_be_i16(data + 3) / 100.0f;
}

static void esc_peak_parse_battery_energy(const uint8_t *data, uint8_t len) {
  if (len != PEAK_PACKET_BATTERY_ENERGY_LEN) {
    return;
  }

  esc_peak_data.watt_hours = read_be_u16(data) / 10.0f;
  esc_peak_data.amp_hours = read_be_u16(data + 2) / 100.0f;
}

static void esc_peak_parse_motor_status(const uint8_t *data, uint8_t len) {
  if (len != PEAK_PACKET_MOTOR_STATUS_LEN) {
    return;
  }

  esc_peak_data.motor_temperature = (int8_t)data[0];
  esc_peak_data.controller_temperature = (int8_t)data[1];
  esc_peak_data.motor_current = read_be_i16(data + 2) / 100.0f;
  esc_peak_data.motor_rpm = read_be_u16(data + 4);
}

static void esc_peak_parse_controller_state(const uint8_t *data, uint8_t len) {
  if (len != PEAK_PACKET_CONTROLLER_STATE_LEN) {
    return;
  }

  esc_peak_data.assist_level = data[0];
  esc_peak_data.support_mode = (cycleiq_support_mode_t)data[1];
  esc_peak_data.ride_mode = (cycleiq_ride_mode_t)data[2];
}

static void esc_peak_parse_live_status(const uint8_t *data, uint8_t len) {
  if (len != PEAK_PACKET_LIVE_STATUS_LEN) {
    return;
  }

  esc_peak_data.speed = read_be_u16(data) / 100.0f;
  esc_peak_data.power = read_be_u16(data + 2);
}

static void esc_peak_parse_trip_primary(const uint8_t *data, uint8_t len) {
  if (len != PEAK_PACKET_TRIP_PRIMARY_LEN) {
    return;
  }

  esc_peak_data.trip_distance = read_be_u32(data) / 1000.0f;
  esc_peak_data.trip_time = (float)read_be_u32(data + 4);
}

static void esc_peak_parse_trip_secondary(const uint8_t *data, uint8_t len) {
  if (len != PEAK_PACKET_TRIP_SECONDARY_LEN) {
    return;
  }

  esc_peak_data.trip_average_speed = read_be_u16(data) / 100.0f;
  esc_peak_data.trip_estimated_range = data[2];
}

static const char *esc_peak_version_support_name(
    cycleiq_version_support_t support) {
  switch (support) {
  case CYCLEIQ_VERSION_SUPPORTED:
    return "supported";
  case CYCLEIQ_VERSION_PARTIALLY_SUPPORTED:
    return "partially supported";
  case CYCLEIQ_VERSION_UNSUPPORTED:
  default:
    return "unsupported";
  }
}

static void esc_peak_parse_protocol_version(const cycleiq_frame_t *frame) {
  cycleiq_version_t remote_protocol;
  cycleiq_version_t remote_sdk;
  if (!cycleiq_read_protocol_version(frame, &remote_protocol, &remote_sdk)) {
    ESP_LOGW(TAG, "invalid protocol version telemetry frame");
    return;
  }

  cycleiq_version_t local_protocol = cycleiq_protocol_version();
  cycleiq_version_support_t support =
      cycleiq_protocol_support_status(local_protocol, remote_protocol);

  ESP_LOGI(TAG,
           "ESC protocol %u.%u.%u, SDK %u.%u.%u: %s by local protocol "
           "%u.%u.%u",
           remote_protocol.major, remote_protocol.minor, remote_protocol.patch,
           remote_sdk.major, remote_sdk.minor, remote_sdk.patch,
           esc_peak_version_support_name(support), local_protocol.major,
           local_protocol.minor, local_protocol.patch);
}

static bool esc_peak_is_config_packet(peak_packet_type_t packet_type) {
  return packet_type == PEAK_PACKET_TYPE_CONFIG_FIELD ||
         packet_type == PEAK_PACKET_TYPE_CONFIG_SNAPSHOT ||
         packet_type == PEAK_PACKET_TYPE_CONFIG_ACK;
}

void esc_peak_parse_data(uint32_t id, const uint8_t *data, uint8_t len,
                         void *user_data) {
  (void)user_data;

  cycleiq_frame_t frame;
  if (!cycleiq_frame_from_can(&frame, id, data, len) ||
      !cycleiq_frame_is_for_node(&frame, PEAK_CAN_ID)) {
    return;
  }

  peak_packet_type_t packet_type =
      (peak_packet_type_t)cycleiq_frame_type(&frame);
  if (packet_type == PEAK_PACKET_TYPE_PROTOCOL_VERSION) {
    esc_peak_parse_protocol_version(&frame);
    return;
  }

  if (esc_peak_is_config_packet(packet_type)) {
    return;
  }

  if (!esc_peak_ensure_data_mutex()) {
    return;
  }

  xSemaphoreTake(esc_peak_data_mutex, portMAX_DELAY);

  switch (packet_type) {
  case PEAK_PACKET_TYPE_BATTERY_STATUS:
    esc_peak_parse_battery_status(frame.data, frame.len);
    break;
  case PEAK_PACKET_TYPE_BATTERY_ENERGY:
    esc_peak_parse_battery_energy(frame.data, frame.len);
    break;
  case PEAK_PACKET_TYPE_MOTOR_STATUS:
    esc_peak_parse_motor_status(frame.data, frame.len);
    break;
  case PEAK_PACKET_TYPE_CONTROLLER_STATE:
    esc_peak_parse_controller_state(frame.data, frame.len);
    break;
  case PEAK_PACKET_TYPE_LIVE_STATUS:
    esc_peak_parse_live_status(frame.data, frame.len);
    break;
  case PEAK_PACKET_TYPE_TRIP_PRIMARY:
    esc_peak_parse_trip_primary(frame.data, frame.len);
    break;
  case PEAK_PACKET_TYPE_TRIP_SECONDARY:
    esc_peak_parse_trip_secondary(frame.data, frame.len);
    break;
  default:
    break;
  }

  xSemaphoreGive(esc_peak_data_mutex);
}

void esc_peak_init(void) {
  if (!esc_peak_ensure_data_mutex()) {
    ESP_LOGE(TAG, "failed to create PEAK ESC data mutex");
    return;
  }

  esp_err_t ret =
      can_register_cb(CYCLEIQ_CAN_FRAME_ID(PEAK_CAN_ID, 0), 0xFF00,
                      esc_peak_parse_data, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to register PEAK ESC CAN callback: %s",
             esp_err_to_name(ret));
    return;
  }

  ret = esc_peak_request_protocol_version();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "failed to request ESC protocol version: %s",
             esp_err_to_name(ret));
  }
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
