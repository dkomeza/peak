#include "esc/esc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#if CONFIG_PEAK_ESC_BACKEND_KT
#include "esc/kt.h"
#else
#include "esc/cycleiq.h"
#endif

static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static esc_state_t s_state;
static esc_update_cb_t s_update_callback;
static void *s_update_context;
static bool s_initialized;

#if CONFIG_PEAK_ESC_BACKEND_KT
static void kt_update(const esc_kt_snapshot_t *snapshot, void *context) {
  (void)context;
  if (snapshot == NULL) {
    return;
  }

  esc_state_t update = {
      .valid_fields = ESC_STATE_GEAR | ESC_STATE_RIDE_MODE | ESC_STATE_WALK,
      .gear = snapshot->gear,
      .ride_mode = snapshot->ride_mode,
      .walk_active = snapshot->walk_active,
  };
  if (snapshot->has_telemetry) {
    update.valid_fields |= ESC_STATE_SPEED | ESC_STATE_POWER |
                           ESC_STATE_MOTOR_TEMP | ESC_STATE_BATTERY_PERCENT;
    update.speed_kph = snapshot->speed_kph;
    update.power_w = (int16_t)snapshot->power_w;
    update.motor_temp_c = snapshot->motor_temp_c;
    update.battery_percent = snapshot->battery_percent;
  }
  esc_publish_state(&update);
}
#endif

static void copy_field(esc_state_t *target, const esc_state_t *source,
                       uint32_t field) {
  if ((source->valid_fields & field) == 0) {
    return;
  }

  target->valid_fields |= field;
  switch (field) {
  case ESC_STATE_GEAR:
    target->gear = source->gear;
    break;
  case ESC_STATE_SUPPORT_MODE:
    target->support_mode = source->support_mode;
    break;
  case ESC_STATE_RIDE_MODE:
    target->ride_mode = source->ride_mode;
    break;
  case ESC_STATE_WALK:
    target->walk_active = source->walk_active;
    break;
  case ESC_STATE_SPEED:
    target->speed_kph = source->speed_kph;
    break;
  case ESC_STATE_POWER:
    target->power_w = source->power_w;
    break;
  case ESC_STATE_MOTOR_TEMP:
    target->motor_temp_c = source->motor_temp_c;
    break;
  case ESC_STATE_CONTROLLER_TEMP:
    target->controller_temp_c = source->controller_temp_c;
    break;
  case ESC_STATE_BATTERY_PERCENT:
    target->battery_percent = source->battery_percent;
    break;
  case ESC_STATE_BATTERY_VOLTAGE:
    target->battery_voltage_v = source->battery_voltage_v;
    break;
  default:
    break;
  }
}

void esc_publish_state(const esc_state_t *update) {
  if (update == NULL || update->valid_fields == 0) {
    return;
  }

  esc_state_t state;
  esc_update_cb_t callback;
  void *context;

  portENTER_CRITICAL(&s_state_lock);
  copy_field(&s_state, update, ESC_STATE_GEAR);
  copy_field(&s_state, update, ESC_STATE_SUPPORT_MODE);
  copy_field(&s_state, update, ESC_STATE_RIDE_MODE);
  copy_field(&s_state, update, ESC_STATE_WALK);
  copy_field(&s_state, update, ESC_STATE_SPEED);
  copy_field(&s_state, update, ESC_STATE_POWER);
  copy_field(&s_state, update, ESC_STATE_MOTOR_TEMP);
  copy_field(&s_state, update, ESC_STATE_CONTROLLER_TEMP);
  copy_field(&s_state, update, ESC_STATE_BATTERY_PERCENT);
  copy_field(&s_state, update, ESC_STATE_BATTERY_VOLTAGE);
  state = s_state;
  callback = s_update_callback;
  context = s_update_context;
  portEXIT_CRITICAL(&s_state_lock);

  if (callback != NULL) {
    callback(&state, context);
  }
}

esp_err_t esc_set_update_callback(esc_update_cb_t callback, void *user_ctx) {
  if (callback == NULL || s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  s_update_callback = callback;
  s_update_context = user_ctx;
  return ESP_OK;
}

esp_err_t esc_init(void) {
  if (s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

#if CONFIG_PEAK_ESC_BACKEND_KT
  esp_err_t ret = esc_kt_set_update_callback(kt_update, NULL);
  if (ret == ESP_OK) {
    ret = esc_kt_init();
  }
#else
  esp_err_t ret = esc_cycleiq_init();
#endif
  if (ret == ESP_OK) {
    s_initialized = true;
#if CONFIG_PEAK_ESC_BACKEND_KT
    esc_kt_snapshot_t snapshot;
    if (esc_kt_get_snapshot(&snapshot) == ESP_OK) {
      kt_update(&snapshot, NULL);
    }
#endif
  }
  return ret;
}

void esc_get_state(esc_state_t *out) {
  if (out == NULL) {
    return;
  }

  portENTER_CRITICAL(&s_state_lock);
  *out = s_state;
  portEXIT_CRITICAL(&s_state_lock);
}

esp_err_t esc_gear_up(void) {
#if CONFIG_PEAK_ESC_BACKEND_KT
  return esc_kt_gear_up();
#else
  return esc_cycleiq_gear_up();
#endif
}

esp_err_t esc_gear_down(void) {
#if CONFIG_PEAK_ESC_BACKEND_KT
  return esc_kt_gear_down();
#else
  return esc_cycleiq_gear_down();
#endif
}

esp_err_t esc_set_support_mode(esc_support_mode_t mode) {
#if CONFIG_PEAK_ESC_BACKEND_KT
  (void)mode;
  return ESP_ERR_NOT_SUPPORTED;
#else
  return esc_cycleiq_set_support_mode(mode);
#endif
}

esp_err_t esc_set_ride_mode(esc_ride_mode_t mode) {
#if CONFIG_PEAK_ESC_BACKEND_KT
  return esc_kt_set_ride_mode(mode);
#else
  return esc_cycleiq_set_ride_mode(mode);
#endif
}

esp_err_t esc_set_walk(bool enabled) {
#if CONFIG_PEAK_ESC_BACKEND_KT
  return esc_kt_set_walk(enabled);
#else
  return esc_cycleiq_set_walk(enabled);
#endif
}
