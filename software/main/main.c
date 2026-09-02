#include "display/display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task.h>
#include <esp_timer.h>
#include <inttypes.h>
#include <nvs_flash.h>

#include "boot/boot.h"
#include "buttons.h"
#include "driver/i2c_master.h"
#include "power.h"

#include "display/display.h"

#include "io/battery.h"
#include "io/ltr329.h"
#include "io/t117.h"

#define PEAK_APP_TASK_STACK_SIZE 8192

static const char *TAG = "peak_app";
static i2c_master_bus_handle_t bus_handle;

void i2c_master_init() {
  i2c_master_bus_config_t bus_config = {.sda_io_num = 31,
                                        .scl_io_num = 30,
                                        .clk_source = I2C_CLK_SRC_DEFAULT,
                                        .flags = {
                                            .enable_internal_pullup = false,
                                        }};
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
}

static void handle_button_power_long(void) {
  ESP_LOGI(TAG, "POWER long press: entering deep sleep");
  display_sleep();
}

static void handle_button_power_long_stop(void) {
  ESP_LOGI(TAG, "POWER long press released");
  power_enter_deep_sleep();
}

static void handle_boot_mountain_mode(void) {
  printf("Booting into Mountain mode...\n");
}

static void nvs_init(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
}

static void peak_app_task(void *arg) {
  (void)arg;
  nvs_init();
  buttons_init();

  boot(handle_boot_mountain_mode);
  ESP_ERROR_CHECK(display_start());

  i2c_master_init();
  ltr329_sensor_init(&bus_handle);
  t117_sensor_init(&bus_handle);

  battery_monitor_init();

  buttons_on(BTN_POWER, BTN_EVENT_LONG_PRESS_START, handle_button_power_long);
  buttons_on(BTN_POWER, BTN_EVENT_LONG_PRESS_END,
             handle_button_power_long_stop);

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void app_main(void) {
  BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
      peak_app_task, "peak_app", PEAK_APP_TASK_STACK_SIZE, NULL,
      ESP_TASK_MAIN_PRIO, NULL, ESP_TASK_MAIN_CORE,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);

  ESP_ERROR_CHECK(ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
