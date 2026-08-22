#include "model.h"

bool display_ui_model_apply(display_ui_model_t *model,
                            const display_event_t *event) {
  if (model == NULL || event == NULL) {
    return false;
  }

  switch (event->type) {
  case DISPLAY_EVENT_CONTROL_STATE:
  case DISPLAY_EVENT_ESC_CONTROLLER_STATE:
    model->gear = event->data.control.gear;
    model->support_mode = event->data.control.support_mode;
    model->ride_mode = event->data.control.ride_mode;
    model->has_control_data = true;
    return true;
  case DISPLAY_EVENT_ESC_WALK_STATE:
    model->walk_active = event->data.control.walk_active;
    return true;
  case DISPLAY_EVENT_ESC_BATTERY:
    model->battery_percent = event->data.esc_battery.percent;
    model->battery_voltage_v = event->data.esc_battery.voltage_v;
    model->has_battery_data = true;
    return true;
  case DISPLAY_EVENT_ESC_LIVE:
    model->speed_kph = event->data.live.speed_kph;
    model->power_w = event->data.live.power_w;
    model->has_live_data = true;
    return true;
  default:
    return false;
  }
}
