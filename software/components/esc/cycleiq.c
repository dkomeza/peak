#include "esc/cycleiq.h"

#include "connection/can.h"
#include "cycleiq_protocol.h"
#include "esp_log.h"
#include "esc/internal.h"

static const char *TAG = "esc_cycleiq";
static bool s_initialized;

static esp_err_t send_frame(bool built, const cycleiq_frame_t *frame) {
  if (!built || frame == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return can_send(frame->id, frame->data, frame->len, 0);
}

static esp_err_t send_empty(bool (*build)(cycleiq_frame_t *)) {
  cycleiq_frame_t frame;
  return send_frame(build(&frame), &frame);
}

static void handle_frame(uint32_t id, const uint8_t *data, uint8_t len,
                         void *context) {
  (void)context;

  cycleiq_frame_t frame;
  if (!cycleiq_frame_from_can(&frame, id, data, len) ||
      !cycleiq_frame_is_for_node(&frame, CYCLEIQ_DISPLAY_CAN_ID)) {
    return;
  }

  esc_state_t update = {0};
  switch (cycleiq_frame_type(&frame)) {
  case CYCLEIQ_TELEMETRY_STATE: {
    cycleiq_support_mode_t support_mode;
    cycleiq_ride_mode_t ride_mode;
    if (!cycleiq_read_state(&frame, &update.gear, &support_mode, &ride_mode,
                            &update.walk_active)) {
      return;
    }
    update.support_mode = (esc_support_mode_t)support_mode;
    update.ride_mode = (esc_ride_mode_t)ride_mode;
    update.valid_fields = ESC_STATE_GEAR | ESC_STATE_SUPPORT_MODE |
                          ESC_STATE_RIDE_MODE | ESC_STATE_WALK;
    break;
  }
  case CYCLEIQ_TELEMETRY_LIVE: {
    uint16_t speed_ckph;
    if (!cycleiq_read_live(&frame, &speed_ckph, &update.power_w)) {
      return;
    }
    update.speed_kph = speed_ckph / 100.0f;
    update.valid_fields = ESC_STATE_SPEED | ESC_STATE_POWER;
    break;
  }
  case CYCLEIQ_TELEMETRY_THERMALS:
    if (!cycleiq_read_thermals(&frame, &update.motor_temp_c,
                                &update.controller_temp_c)) {
      return;
    }
    update.valid_fields = ESC_STATE_MOTOR_TEMP | ESC_STATE_CONTROLLER_TEMP;
    break;
  case CYCLEIQ_TELEMETRY_BATTERY: {
    uint16_t voltage_cv;
    if (!cycleiq_read_battery(&frame, &update.battery_percent, &voltage_cv)) {
      return;
    }
    update.battery_voltage_v = voltage_cv / 100.0f;
    update.valid_fields = ESC_STATE_BATTERY_PERCENT | ESC_STATE_BATTERY_VOLTAGE;
    break;
  }
  default:
    return;
  }

  esc_publish_state(&update);
}

esp_err_t esc_cycleiq_init(void) {
  if (s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = can_init();
  if (ret != ESP_OK) {
    return ret;
  }

  ret = can_register_cb(CYCLEIQ_CAN_FRAME_ID(CYCLEIQ_DISPLAY_CAN_ID, 0),
                        0xFF00, handle_frame, NULL);
  if (ret != ESP_OK) {
    return ret;
  }

  s_initialized = true;
  ret = send_empty(cycleiq_sync_request);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "failed to request ESC state: %s", esp_err_to_name(ret));
  }
  return ESP_OK;
}

esp_err_t esc_cycleiq_gear_up(void) { return send_empty(cycleiq_gear_up); }

esp_err_t esc_cycleiq_gear_down(void) {
  return send_empty(cycleiq_gear_down);
}

esp_err_t esc_cycleiq_set_support_mode(esc_support_mode_t mode) {
  if (mode > ESC_SUPPORT_MODE_TORQUE) {
    return ESP_ERR_INVALID_ARG;
  }

  cycleiq_frame_t frame;
  return send_frame(cycleiq_set_support_mode(
                        &frame, (cycleiq_support_mode_t)mode),
                    &frame);
}

esp_err_t esc_cycleiq_set_ride_mode(esc_ride_mode_t mode) {
  if (mode > ESC_RIDE_MODE_MOUNTAIN) {
    return ESP_ERR_INVALID_ARG;
  }

  cycleiq_frame_t frame;
  return send_frame(cycleiq_set_ride_mode(&frame, (cycleiq_ride_mode_t)mode),
                    &frame);
}

esp_err_t esc_cycleiq_set_walk(bool enabled) {
  cycleiq_frame_t frame;
  return send_frame(cycleiq_set_walk(&frame, enabled), &frame);
}
