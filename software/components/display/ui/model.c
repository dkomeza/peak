#include "model.h"

bool display_ui_model_apply(display_ui_model_t *model,
                            const display_event_t *event) {
  if (model == NULL || event == NULL) {
    return false;
  }

  switch (event->type) {
  case DISPLAY_EVENT_CONTROL_STATE:
    model->gear = event->data.control.gear;
    model->support_mode = event->data.control.support_mode;
    model->ride_mode = event->data.control.ride_mode;
    model->walk_active = event->data.control.walk_active;
    model->has_gear = true;
    model->has_support_mode = true;
    model->has_ride_mode = true;
    return true;
  case DISPLAY_EVENT_ESC_STATE: {
    const typeof(event->data.esc_state) *state = &event->data.esc_state;
    uint32_t valid = state->valid_fields;

    if (valid & DISPLAY_ESC_STATE_GEAR) {
      model->gear = state->gear;
      model->has_gear = true;
    }
    if (valid & DISPLAY_ESC_STATE_SUPPORT_MODE) {
      model->support_mode = state->support_mode;
      model->has_support_mode = true;
    }
    if (valid & DISPLAY_ESC_STATE_RIDE_MODE) {
      model->ride_mode = state->ride_mode;
      model->has_ride_mode = true;
    }
    if (valid & DISPLAY_ESC_STATE_WALK) {
      model->walk_active = state->walk_active;
    }
    if (valid & DISPLAY_ESC_STATE_SPEED) {
      model->speed_kph = state->speed_kph;
      model->has_speed = true;
    }
    if (valid & DISPLAY_ESC_STATE_POWER) {
      model->power_w = state->power_w;
      model->has_power = true;
    }
    if (valid & DISPLAY_ESC_STATE_MOTOR_TEMP) {
      model->motor_temp_c = state->motor_temp_c;
      model->has_motor_temp = true;
    }
    if (valid & DISPLAY_ESC_STATE_CONTROLLER_TEMP) {
      model->controller_temp_c = state->controller_temp_c;
      model->has_controller_temp = true;
    }
    if (valid & DISPLAY_ESC_STATE_BATTERY_PERCENT) {
      model->battery_percent = state->battery_percent;
      model->has_battery_percent = true;
    }
    if (valid & DISPLAY_ESC_STATE_BATTERY_VOLTAGE) {
      model->battery_voltage_v = state->battery_voltage_v;
      model->has_battery_voltage = true;
    }
    return true;
  }
  default:
    return false;
  }
}
