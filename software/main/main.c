#include "button.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons.h"
#include "driver/i2c_master.h"
#include "ltr329.h"
#include "t117.h"

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

void button_up_pressed(void) { printf("UP button pressed!\n"); }
void button_power_pressed(void) { printf("POWER button pressed!\n"); }
void button_down_pressed(void) { printf("DOWN button pressed!\n"); }

void app_main(void) {
  i2c_master_bus_handle_t bus_handle;
  i2c_master_init(&bus_handle);

  ltr329_sensor_init(&bus_handle);
  t117_sensor_init(&bus_handle);

  buttons_init();

  button_on(BTN_UP, BTN_EVENT_DOWN, button_up_pressed);
  button_on(BTN_POWER, BTN_EVENT_DOWN, button_power_pressed);
  button_on(BTN_DOWN, BTN_EVENT_DOWN, button_down_pressed);

  while (1) {
    float lux = ltr329_read_lux();
    float temp = t117_read_temperature();
    printf("Ambient Light: %.2f lux\n", lux);
    printf("Temperature: %.2f °C\n", temp);
    printf("-----------------------------\n");
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
