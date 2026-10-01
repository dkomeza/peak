#include "display_adapter.h"

#include "display/display.h"
#include "esc/esc.h"
#include "esp_log.h"

static const char *TAG = "display_adapter";

static void publish_display_state(const display_state_t *state) {
  esp_err_t ret = display_update(state);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "display update rejected: %s", esp_err_to_name(ret));
  }
}

static void on_esc_update(const esc_state_t *state, void *user_ctx) {
  (void)user_ctx;
  if (state == NULL) {
    return;
  }

  display_state_t display_state = {0};
  if ((state->valid_fields & ESC_STATE_SPEED) != 0) {
    display_state.valid_fields |= DISPLAY_STATE_SPEED;
    display_state.speed_kph = state->speed_kph;
  }
  if ((state->valid_fields & ESC_STATE_BATTERY_VOLTAGE) != 0) {
    display_state.valid_fields |= DISPLAY_STATE_BATTERY_VOLTAGE;
    display_state.battery_voltage_v = state->battery_voltage_v;
  }
  if ((state->valid_fields & ESC_STATE_MOTOR_TEMP) != 0) {
    display_state.valid_fields |= DISPLAY_STATE_MOTOR_TEMP;
    display_state.motor_temp_c = state->motor_temp_c;
  }
  if ((state->valid_fields & ESC_STATE_CONTROLLER_TEMP) != 0) {
    display_state.valid_fields |= DISPLAY_STATE_CONTROLLER_TEMP;
    display_state.controller_temp_c = state->controller_temp_c;
  }
  if ((state->valid_fields & ESC_STATE_GEAR) != 0) {
    display_state.valid_fields |= DISPLAY_STATE_GEAR;
    display_state.gear = state->gear;
  }
  if ((state->valid_fields & ESC_STATE_SUPPORT_MODE) != 0) {
    switch (state->support_mode) {
    case ESC_SUPPORT_MODE_PAS:
      display_state.support_mode = DISPLAY_SUPPORT_MODE_PAS;
      display_state.valid_fields |= DISPLAY_STATE_SUPPORT_MODE;
      break;
    case ESC_SUPPORT_MODE_TORQUE:
      display_state.support_mode = DISPLAY_SUPPORT_MODE_TORQUE;
      display_state.valid_fields |= DISPLAY_STATE_SUPPORT_MODE;
      break;
    default:
      break;
    }
  }

  if (display_state.valid_fields != 0) {
    publish_display_state(&display_state);
  }
}

esp_err_t display_adapter_start(void) {
  return esc_set_update_callback(on_esc_update, NULL);
}
