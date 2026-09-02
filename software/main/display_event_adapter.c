#include "display_event_adapter.h"

#include "display/display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esc/esc.h"

static const char *TAG = "display_adapter";

static void publish_display_event(display_event_t *event) {
  event->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  esp_err_t ret = display_event_publish(event);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "display event dropped: %s", esp_err_to_name(ret));
  }
}

static void on_esc_update(const esc_state_t *state, void *user_ctx) {
  (void)user_ctx;
  if (state == NULL) {
    return;
  }

  display_event_t event = {
      .type = DISPLAY_EVENT_ESC_STATE,
      .data.esc_state = {
          .valid_fields = state->valid_fields,
          .gear = state->gear,
          .support_mode = state->support_mode,
          .ride_mode = state->ride_mode,
          .walk_active = state->walk_active,
          .speed_kph = state->speed_kph,
          .power_w = state->power_w,
          .motor_temp_c = state->motor_temp_c,
          .controller_temp_c = state->controller_temp_c,
          .battery_percent = state->battery_percent,
          .battery_voltage_v = state->battery_voltage_v,
      },
  };

  publish_display_event(&event);
}

esp_err_t display_event_adapter_start(void) {
  return esc_set_update_callback(on_esc_update, NULL);
}
