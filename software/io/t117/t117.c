#include "include/t117.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#define T117_ADDR 0x40
#define T117_FREQUENCY_HZ 100000

i2c_master_dev_handle_t t117_dev_handle;
static const char *TAG = "T117";
static const float SMOOTHING_FACTOR = 0.2f;

volatile float t117_temperature = 0.0f;

void t117_task(void *arg);
float t117_read_temp_high_res(i2c_master_dev_handle_t dev_handle);

void t117_sensor_init(i2c_master_bus_handle_t *bus_handle) {
  ESP_LOGI(TAG, "Initializing sensor...");
  i2c_device_config_t dev_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = T117_ADDR,
      .scl_speed_hz = T117_FREQUENCY_HZ,
  };
  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(*bus_handle, &dev_config, &t117_dev_handle));
  ESP_LOGI(TAG, "Sensor initialized successfully!");

  ESP_LOGI(TAG, "Starting sensor task...");
  xTaskCreatePinnedToCore(t117_task, "ltr329_task", 4096, NULL, 0, NULL, 0);
}

float t117_read_temperature() { return t117_temperature; }

void t117_task(void *arg) {
  uint8_t config_data[2] = {0x05, 0x79};
  i2c_master_transmit(t117_dev_handle, config_data, 2, -1);

  while (1) {
    t117_temperature = t117_read_temp_high_res(t117_dev_handle);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

float t117_read_temp_high_res(i2c_master_dev_handle_t dev_handle) {
  uint8_t cmd[] = {0x04, 0xC0};
  i2c_master_transmit(dev_handle, cmd, 2, -1);

  vTaskDelay(pdMS_TO_TICKS(20));

  uint8_t reg = 0x00;
  uint8_t data[2];
  i2c_master_transmit_receive(dev_handle, &reg, 1, data, 2, -1);

  int16_t raw = (int16_t)((data[1] << 8) | data[0]);
  return ((float)raw / 256.0f) + 25.0f;
}

float smooth_temperature(float new_temp) {
  float a = SMOOTHING_FACTOR;

  return (a * new_temp) + ((1 - a) * t117_temperature);
}
