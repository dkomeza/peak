#include "display_event_adapter.h"

#include "display/display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esc/peak.h"

static const char *TAG = "display_adapter";

static void publish_display_event(display_event_t *event) {
  event->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  esp_err_t ret = display_event_publish(event);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "display event dropped: %s", esp_err_to_name(ret));
  }
}

static void on_esc_update(const esc_peak_update_t *update, void *user_ctx) {
  (void)user_ctx;
  if (update == NULL) {
    return;
  }

  display_event_t event = {0};
  switch (update->type) {
  case ESC_PEAK_UPDATE_BATTERY_STATUS:
    event.type = DISPLAY_EVENT_ESC_BATTERY;
    event.data.esc_battery = (typeof(event.data.esc_battery)){
        .percent = update->data.battery.percentage,
        .voltage_v = update->data.battery.voltage_v,
        .current_a = update->data.battery.current_a,
    };
    break;
  case ESC_PEAK_UPDATE_BATTERY_ENERGY:
    event.type = DISPLAY_EVENT_ESC_ENERGY;
    event.data.esc_energy = (typeof(event.data.esc_energy)){
        .watt_hours = update->data.energy.watt_hours,
        .amp_hours = update->data.energy.amp_hours,
    };
    break;
  case ESC_PEAK_UPDATE_MOTOR_STATUS:
    event.type = DISPLAY_EVENT_ESC_MOTOR;
    event.data.motor = (typeof(event.data.motor)){
        .motor_c = update->data.motor.motor_c,
        .controller_c = update->data.motor.controller_c,
        .current_a = update->data.motor.current_a,
        .rpm = update->data.motor.rpm,
    };
    break;
  case ESC_PEAK_UPDATE_CONTROLLER_STATE:
    event.type = DISPLAY_EVENT_CONTROL_STATE;
    event.data.control = (typeof(event.data.control)){
        .gear = update->data.controller.assist_level,
        .support_mode = update->data.controller.support_mode,
        .ride_mode = update->data.controller.ride_mode,
    };
    break;
  case ESC_PEAK_UPDATE_LIVE_STATUS:
    event.type = DISPLAY_EVENT_ESC_LIVE;
    event.data.live = (typeof(event.data.live)){
        .speed_kph = update->data.live.speed_kph,
        .power_w = update->data.live.power_w,
    };
    break;
  case ESC_PEAK_UPDATE_TRIP_PRIMARY:
    event.type = DISPLAY_EVENT_ESC_TRIP;
    event.data.trip = (typeof(event.data.trip)){
        .distance_km = update->data.trip_primary.distance_km,
        .elapsed_s = update->data.trip_primary.time_s,
    };
    break;
  case ESC_PEAK_UPDATE_TRIP_SECONDARY:
    event.type = DISPLAY_EVENT_ESC_TRIP;
    event.data.trip = (typeof(event.data.trip)){
        .average_kph = update->data.trip_secondary.average_speed_kph,
        .range_km = update->data.trip_secondary.estimated_range_km,
    };
    break;
  case ESC_PEAK_UPDATE_WALK_STATE:
    event.type = DISPLAY_EVENT_CONTROL_STATE;
    event.data.control.walk_active = update->data.walk.active;
    break;
  default:
    return;
  }

  publish_display_event(&event);
}

esp_err_t display_event_adapter_start(void) {
  return esc_peak_set_update_callback(on_esc_update, NULL);
}
