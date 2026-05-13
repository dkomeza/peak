#include "ui/ui.h"

#include "esp_check.h"
#include "ui/dashboard.h"
#include "ui/style.h"

static const char *TAG = "ui";

static peak_dashboard_data_t make_mock_data(uint32_t tick) {
  uint16_t speed = 4 + (tick % 18);
  return (peak_dashboard_data_t){
      .speed_kmh = speed,
      .battery_percent = 84 - (tick % 9),
      .power_watts = 67 + ((tick * 17) % 260),
      .assist_segments = 1 + (tick % 5),
      .estimated_range_km = 85 - (tick % 12),
      .trip_distance_km = 24.8f + ((float)(tick % 20) / 10.0f),
      .ride_time_minutes = 84 + (tick % 40),
      .average_speed_kmh = 22.4f + ((float)(tick % 8) / 10.0f),
      .motor_temp_c = 62 + (tick % 5),
      .controller_temp_c = 80 + (tick % 4),
      .connected = true,
      .mode_label = "ECO - TORQUE",
  };
}

static void mock_timer_cb(lv_timer_t *timer) {
  uint32_t *tick = (uint32_t *)lv_timer_get_user_data(timer);
  peak_dashboard_data_t data = make_mock_data(*tick);
  peak_dashboard_update(&data);
  (*tick)++;
}

esp_err_t ui_init(void) {
  peak_ui_style_init();

  lv_obj_t *screen = lv_obj_create(NULL);
  ESP_RETURN_ON_FALSE(screen != NULL, ESP_ERR_NO_MEM, TAG,
                      "failed to create dashboard screen");

  ESP_RETURN_ON_ERROR(peak_dashboard_create(screen), TAG,
                      "failed to create dashboard");

  static uint32_t mock_tick = 0;
  peak_dashboard_data_t initial_data = make_mock_data(mock_tick);
  peak_dashboard_update(&initial_data);
  mock_tick++;

  lv_screen_load(screen);

  lv_timer_t *timer = lv_timer_create(mock_timer_cb, 1000, &mock_tick);
  ESP_RETURN_ON_FALSE(timer != NULL, ESP_ERR_NO_MEM, TAG,
                      "failed to create mock data timer");

  return ESP_OK;
}
