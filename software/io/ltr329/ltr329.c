#include "ltr329.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#define LTR329_ADDR 0x29
#define LTR329_FREQUENCY_HZ 100000
#define LTR329_BASE_GAIN 1.0f
#define LTR329_BASE_ITIME 100.0f // ms

i2c_master_dev_handle_t ltr329_dev_handle;
static const char *TAG = "LTR329";
static float GAIN = 1.0f;
static float ITIME = 100.0f; // Integration time in ms
static float SMOOTHING_FACTOR = 0.2f;

volatile float ltr329_lux = 0.0f;

void ltr329_task(void *arg);
float calculate_lux(uint16_t ch0, uint16_t ch1);
float smooth_lux(float new_lux);

void ltr329_sensor_init(i2c_master_bus_handle_t *bus_handle) {
  ESP_LOGI(TAG, "Initializing sensor...");
  i2c_device_config_t dev_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = LTR329_ADDR,
      .scl_speed_hz = LTR329_FREQUENCY_HZ,
  };
  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(*bus_handle, &dev_config, &ltr329_dev_handle));
  ESP_LOGI(TAG, "Sensor initialized successfully!");

  ESP_LOGI(TAG, "Starting sensor task...");
  xTaskCreatePinnedToCore(ltr329_task, "ltr329_task", 4096, NULL, 0, NULL, 0);
}

float ltr329_read_lux() { return ltr329_lux; }

void ltr329_task(void *arg) {
  ESP_LOGI(TAG, "Setting up sensor configuration...");
  uint8_t als_config_data[2] = {0x80, 0x01};
  i2c_master_transmit(ltr329_dev_handle, als_config_data, 2, -1);

  uint8_t meas_config_data[2] = {0x85, 0x03};
  i2c_master_transmit(ltr329_dev_handle, meas_config_data, 2, -1);

  uint8_t reg_addr = 0x88; // ALS data register
  uint8_t data[4] = {0};

  while (1) {
    esp_err_t ret = i2c_master_transmit_receive(ltr329_dev_handle, &reg_addr, 1,
                                                data, 4, -1);
    if (ret == ESP_OK) {
      uint16_t ch1 = (data[1] << 8) | data[0];
      uint16_t ch0 = (data[3] << 8) | data[2];

      float lux = calculate_lux(ch0, ch1);
      ltr329_lux = smooth_lux(lux);
    } else {
      ESP_LOGE(TAG, "Failed to read lux: %s", esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Read every second
  }
}

float calculate_lux(uint16_t ch0, uint16_t ch1) {
  float gain_factor = GAIN / LTR329_BASE_GAIN;
  float itime_factor = ITIME / LTR329_BASE_ITIME;
  float base = gain_factor * itime_factor;

  float ratio = ch0 + ch1 > 0 ? (float)ch1 / (ch0 + ch1) : 1;
  float lux = 0.0f;

  if (ratio < 0.45) {
    lux = ((1.7743 * ch0) + (1.1059 * ch1)) / base;
  } else if (ratio < 0.64) {
    lux = ((4.2785 * ch0) - (1.9548 * ch1)) / base;
  } else if (ratio < 0.85) {
    lux = ((5.926 * ch0) - (4.2785 * ch1)) / base;
  }

  return lux;
}

float smooth_lux(float new_lux) {
  float a = SMOOTHING_FACTOR;

  return (a * new_lux) + ((1 - a) * ltr329_lux);
}
