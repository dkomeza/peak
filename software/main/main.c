#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "ltr329.h"

static const char *TAG = "PEAK";

void i2c_master_init(i2c_master_bus_handle_t *bus_handle) {
  i2c_master_bus_config_t bus_config = {.sda_io_num = 31,
                                        .scl_io_num = 30,
                                        .clk_source = I2C_CLK_SRC_DEFAULT,
                                        .flags = {
                                            .enable_internal_pullup = false,
                                        }};
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bus_handle));
}

void app_main(void) {
  i2c_master_bus_handle_t bus_handle;
  i2c_master_init(&bus_handle);

  ltr329_sensor_init(&bus_handle);

  while (1) {
    float lux = ltr329_read_lux();
    printf("Ambient Light: %.2f lux\n", lux);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
