#include "battery.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BATTERY_ADC_GPIO 49
#define BATTERY_MONITOR_INTERVAL_MS 1000

#define BATTERY_DIVIDER_100K_100K 1
#define BATTERY_DIVIDER_1M_47K 2

#ifndef BATTERY_DIVIDER
#define BATTERY_DIVIDER BATTERY_DIVIDER_100K_100K
#endif

#if BATTERY_DIVIDER == BATTERY_DIVIDER_100K_100K
#define BATTERY_DIVIDER_TOP_OHMS 100000.0f
#define BATTERY_DIVIDER_BOTTOM_OHMS 100000.0f
#elif BATTERY_DIVIDER == BATTERY_DIVIDER_1M_47K
#define BATTERY_DIVIDER_TOP_OHMS 1000000.0f
#define BATTERY_DIVIDER_BOTTOM_OHMS 47000.0f
#else
#error "Unsupported BATTERY_DIVIDER"
#endif

static const char *TAG = "BATTERY";

static adc_oneshot_unit_handle_t battery_adc_handle;
static adc_cali_handle_t battery_adc_cali_handle;
static adc_channel_t battery_adc_channel;
static bool battery_adc_calibrated;
static bool battery_monitor_started;

volatile float battery_voltage = 0.0f;

static bool battery_adc_calibration_init(adc_unit_t unit,
                                         adc_channel_t channel,
                                         adc_cali_handle_t *out_handle) {
  adc_cali_handle_t handle = NULL;
  bool calibrated = false;
  esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = unit,
      .chan = channel,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
  if (ret == ESP_OK) {
    calibrated = true;
  }
#endif

  *out_handle = handle;
  if (!calibrated) {
    ESP_LOGW(TAG, "ADC calibration unavailable: %s", esp_err_to_name(ret));
  }

  return calibrated;
}

static float battery_adc_mv_to_voltage(int adc_mv) {
  return ((float)adc_mv / 1000.0f) *
         ((BATTERY_DIVIDER_TOP_OHMS + BATTERY_DIVIDER_BOTTOM_OHMS) /
          BATTERY_DIVIDER_BOTTOM_OHMS);
}

float battery_read_voltage(void) { return battery_voltage; }

static void battery_monitor_task(void *arg) {
  (void)arg;

  while (1) {
    int adc_mv = 0;
    esp_err_t ret = ESP_FAIL;

    if (battery_adc_calibrated) {
      ret = adc_oneshot_get_calibrated_result(
          battery_adc_handle, battery_adc_cali_handle, battery_adc_channel,
          &adc_mv);
    }

    if (ret == ESP_OK) {
      battery_voltage = battery_adc_mv_to_voltage(adc_mv);
    } else {
      ESP_LOGW(TAG, "Failed to read battery voltage: %s", esp_err_to_name(ret));
    }

    vTaskDelay(pdMS_TO_TICKS(BATTERY_MONITOR_INTERVAL_MS));
  }
}

void battery_monitor_init() {
  if (battery_monitor_started) {
    return;
  }

  adc_unit_t unit = ADC_UNIT_1;
  esp_err_t ret =
      adc_oneshot_io_to_channel(BATTERY_ADC_GPIO, &unit, &battery_adc_channel);
  ESP_ERROR_CHECK(ret);

  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = unit,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &battery_adc_handle));

  adc_oneshot_chan_cfg_t channel_config = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(
      battery_adc_handle, battery_adc_channel, &channel_config));

  battery_adc_calibrated =
      battery_adc_calibration_init(unit, battery_adc_channel,
                                   &battery_adc_cali_handle);

  xTaskCreatePinnedToCore(battery_monitor_task, "battery_monitor_task", 4096,
                          NULL, 0, NULL, 0);

  battery_monitor_started = true;
  ESP_LOGI(TAG, "Monitoring GPIO%d with %.0f/%.0f ohm divider",
           BATTERY_ADC_GPIO, BATTERY_DIVIDER_TOP_OHMS,
           BATTERY_DIVIDER_BOTTOM_OHMS);
}
