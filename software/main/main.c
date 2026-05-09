#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_task.h>
#include <esp_timer.h>
#include <nvs_flash.h>

// #include "boot/boot.h"
#include "buttons.h"
#include "connection/can.h"
#include "driver/i2c_master.h"

#include "io/battery.h"
#include "io/ltr329.h"
#include "io/t117.h"

#include "vesc/vesc_bridge.h"

#include "wireless/ble_bridge.h"
#include "wireless/udp_bridge.h"
#include "wireless/wifi.h"

#define PEAK_APP_TASK_STACK_SIZE 8192

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

/**
 * Called when the device is booted into mountain mode. This will be used
 * to send appropriate commands to the ESC and change the UI
 */
void mountain_mode_callback(void) {}

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

  // ESP_ERROR_CHECK(wifi_start("DEKANET", "tramwaj55"));
  ESP_ERROR_CHECK(wifi_start_ap());
  ESP_ERROR_CHECK(can_init());

  ESP_ERROR_CHECK(vesc_bridge_init());
  vesc_bridge_start(&transport_udp);

  // battery_monitor_init();

  // boot_mode_t mode = boot(mountain_mode_callback);

  // i2c_master_bus_handle_t bus_handle;
  // i2c_master_init(&bus_handle);
  //
  // ltr329_sensor_init(&bus_handle);
  // t117_sensor_init(&bus_handle);
  //
  // buttons_on(BTN_UP, BTN_EVENT_CLICK, button_up_pressed);
  // buttons_on(BTN_POWER, BTN_EVENT_CLICK, button_power_pressed);
  // buttons_on(BTN_DOWN, BTN_EVENT_CLICK, button_down_pressed);

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
