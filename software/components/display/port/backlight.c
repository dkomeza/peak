#include "backlight.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "io/ltr329.h"
#include <math.h>

#define BACKLIGHT_GPIO GPIO_NUM_10
#define BACKLIGHT_FREQUENCY_HZ 1000U
#define BACKLIGHT_RESOLUTION LEDC_TIMER_10_BIT
#define BACKLIGHT_MAX_DUTY ((1U << BACKLIGHT_RESOLUTION) - 1U)
#define BACKLIGHT_FADE_MS 300U
#define BACKLIGHT_AUTO_UPDATE_MS 250U

static const char *TAG = "backlight";
static uint32_t s_last_auto_update_ms;
static uint8_t s_manual_percent = 100;
static uint8_t s_percent = 100;
static bool s_initialized;
static bool s_enabled;
static bool s_auto_enabled;

static uint32_t percent_to_duty(uint8_t percent) {
  return (BACKLIGHT_MAX_DUTY * percent) / 100U;
}

static uint32_t gamma_correct(uint32_t duty) {
  return (uint32_t)(powf((float)duty / BACKLIGHT_MAX_DUTY, 2.2f) *
                    BACKLIGHT_MAX_DUTY +
                    0.5f);
}

static uint32_t gamma_uncorrect(uint32_t duty) {
  return (uint32_t)(powf((float)duty / BACKLIGHT_MAX_DUTY, 1.0f / 2.2f) *
                    BACKLIGHT_MAX_DUTY +
                    0.5f);
}

static uint8_t lux_to_percent(float lux) {
  if (lux <= 5.0f) {
    return 10;
  }
  if (lux >= 1000.0f) {
    return 100;
  }
  return 10 + (uint8_t)((lux - 5.0f) * 90.0f / 995.0f);
}

static esp_err_t fade_to(uint8_t percent) {
  uint32_t target = percent_to_duty(percent);
  ESP_RETURN_ON_ERROR(ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0),
                      TAG, "Failed to stop active fade");

  uint32_t duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  ESP_RETURN_ON_FALSE(duty != LEDC_ERR_DUTY, ESP_FAIL, TAG,
                      "Failed to read current duty");
  uint32_t start = gamma_uncorrect(duty);
  if (start == target) {
    return ESP_OK;
  }

  ledc_fade_param_config_t params[SOC_LEDC_GAMMA_CURVE_FADE_RANGE_MAX] = {};
  uint32_t range_count;
  ESP_RETURN_ON_ERROR(
      ledc_fill_multi_fade_param_list(
          LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, start, target, 12,
          BACKLIGHT_FADE_MS, gamma_correct,
          SOC_LEDC_GAMMA_CURVE_FADE_RANGE_MAX, params, &range_count),
      TAG, "Failed to configure gamma fade");
  return ledc_set_multi_fade_and_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                       gamma_correct(start), params,
                                       range_count, LEDC_FADE_NO_WAIT);
}

esp_err_t backlight_init(void) {
  ESP_RETURN_ON_FALSE(!s_initialized, ESP_ERR_INVALID_STATE, TAG,
                      "Backlight is already initialized");

  const ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = BACKLIGHT_RESOLUTION,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = BACKLIGHT_FREQUENCY_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG,
                      "Failed to configure PWM timer");

  const ledc_channel_config_t channel = {
      .gpio_num = BACKLIGHT_GPIO,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .timer_sel = LEDC_TIMER_0,
      .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
  };
  ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG,
                      "Failed to configure PWM channel");

  esp_err_t ret = ledc_fade_func_install(0);
  ESP_RETURN_ON_FALSE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE, ret, TAG,
                      "Failed to install LEDC fade service");
  s_initialized = true;
  return ESP_OK;
}

esp_err_t backlight_set_enabled(bool enabled) {
  ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                      "Backlight is not initialized");
  s_enabled = enabled;
  return fade_to(enabled ? s_percent : 0);
}

esp_err_t backlight_set_percent(uint8_t percent) {
  ESP_RETURN_ON_FALSE(percent <= 100U, ESP_ERR_INVALID_ARG, TAG,
                      "Brightness must be between 0 and 100 percent");
  ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                      "Backlight is not initialized");
  s_auto_enabled = false;
  s_manual_percent = percent;
  s_percent = percent;
  return fade_to(s_enabled ? percent : 0);
}

esp_err_t backlight_set_auto_enabled(bool enabled) {
  ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                      "Backlight is not initialized");
  s_auto_enabled = enabled;
  if (!enabled) {
    s_percent = s_manual_percent;
    return fade_to(s_enabled ? s_percent : 0);
  }

  s_last_auto_update_ms =
      (uint32_t)(esp_timer_get_time() / 1000ULL) - BACKLIGHT_AUTO_UPDATE_MS;
  return backlight_service();
}

esp_err_t backlight_service(void) {
  ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                      "Backlight is not initialized");
  if (!s_auto_enabled) {
    return ESP_OK;
  }

  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  if (now_ms - s_last_auto_update_ms < BACKLIGHT_AUTO_UPDATE_MS) {
    return ESP_OK;
  }

  s_last_auto_update_ms = now_ms;
  s_percent = lux_to_percent(ltr329_read_lux());
  return fade_to(s_enabled ? s_percent : 0);
}
