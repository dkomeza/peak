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
#include "connection/can.h"
#include "display_event_adapter.h"
#include "driver/i2c_master.h"
#include "power.h"

#include "io/battery.h"
#include "io/ltr329.h"
#include "io/t117.h"

#include "display/display.h"
#include "esc/peak.h"

#include "vesc/vesc_bridge.h"

#include "wireless/ble_bridge.h"
#include "wireless/ble_ota.h"
#include "wireless/udp_bridge.h"
#include "wireless/wifi.h"

#define PEAK_APP_TASK_STACK_SIZE 8192
#define PEAK_WALK_REFRESH_MS 250U
#define PEAK_POWER_OFF_TX_TIMEOUT_MS 100U

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

static void handle_button_up_click(void) {
  // uint8_t next_gear =
  //     current_gear < PEAK_MAX_GEAR ? current_gear + 1 : current_gear;
  // esp_err_t ret = esc_controller_set_gear(&peak_controller, next_gear);
  // if (ret == ESP_OK) {
  //   current_gear = next_gear;
  // }
  // publish_action_result(DISPLAY_ACTION_GEAR, ret);
  // log_esc_command_result("UP click: gear up", ret);
}

static void handle_button_power_click(void) {
  // if (ignore_boot_power_release) {
  //   ignore_boot_power_release = false;
  //   return;
  // }
  //
  // ESP_LOGI(TAG, "POWER click");
}

static void handle_button_down_click(void) {
  // uint8_t next_gear =
  //     current_gear > PEAK_MIN_GEAR ? current_gear - 1 : current_gear;
  // esp_err_t ret = esc_controller_set_gear(&peak_controller, next_gear);
  // if (ret == ESP_OK) {
  //   current_gear = next_gear;
  // }
  // publish_action_result(DISPLAY_ACTION_GEAR, ret);
  // log_esc_command_result("DOWN click: gear down", ret);
}

static void handle_button_up_long(void) {
  // esc_support_mode_t next_mode = current_support_mode == ESC_SUPPORT_MODE_PAS
  //                                    ? ESC_SUPPORT_MODE_TORQUE
  //                                    : ESC_SUPPORT_MODE_PAS;
  //
  // esp_err_t ret = esc_controller_set_support_mode(&peak_controller,
  // next_mode); if (ret == ESP_OK) {
  //   current_support_mode = next_mode;
  // }
  // publish_action_result(DISPLAY_ACTION_SUPPORT_MODE, ret);
  // log_esc_command_result("UP long press: toggle support mode", ret);
}

static void stop_walk_mode(const char *action);

static void shutdown_controller(void) {
  // stop_walk_mode("POWER long press: stop walk mode");
  //
  // esp_err_t ret = request_controller_power(false);
  // if (ret != ESP_OK) {
  //   return;
  // }
  //
  // ret = can_wait_for_tx(PEAK_POWER_OFF_TX_TIMEOUT_MS);
  // if (ret != ESP_OK) {
  //   ESP_LOGW(TAG, "CycleIQ power-off transmit did not complete: %s",
  //            esp_err_to_name(ret));
  //   return;
  // }
  //
  // ret = display_sleep();
  // if (ret != ESP_OK) {
  //   ESP_LOGW(TAG, "Display sleep failed: %s", esp_err_to_name(ret));
  // }
  //
  // ret = power_enter_deep_sleep();
  // ESP_LOGW(TAG, "Deep sleep was rejected: %s", esp_err_to_name(ret));
}

static void handle_button_power_long(void) {
  ESP_LOGI(TAG, "POWER long press: entering deep sleep");
}

static void handle_button_power_long_stop(void) {
  ESP_LOGI(TAG, "POWER long press released");
  power_enter_deep_sleep();
}

static void handle_boot_mountain_mode(void) {
  printf("Booting into Mountain mode...\n");
  // esp_err_t ret =
  //     esc_controller_set_ride_mode(&peak_controller, ESC_RIDE_MODE_MOUNTAIN);
  // if (ret == ESP_OK) {
  //   current_ride_mode = ESC_RIDE_MODE_MOUNTAIN;
  // }
  // publish_action_result(DISPLAY_ACTION_RIDE_MODE, ret);
  // log_esc_command_result("Mountain boot mode", ret);
}

static void handle_button_power_long_start(void) {}

static void handle_button_down_long_start(void) {
  // if (walk_command_active) {
  //   return;
  // }
  //
  // walk_command_active = true;
  // uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  // esp_err_t ret = esc_controller_set_walk_mode(&peak_controller, true);
  // next_walk_refresh_ms = now_ms + PEAK_WALK_REFRESH_MS;
  // last_walk_refresh_error_ms = ret == ESP_OK ? 0 : now_ms;
  //
  // publish_action_result(DISPLAY_ACTION_WALK_MODE, ret);
  // log_esc_command_result("DOWN long press: start walk mode", ret);
}

static void stop_walk_mode(const char *action) {
  // if (!walk_command_active) {
  //   return;
  // }
  //
  // walk_command_active = false;
  // esp_err_t ret = esc_controller_set_walk_mode(&peak_controller, false);
  // publish_action_result(DISPLAY_ACTION_WALK_MODE, ret);
  // log_esc_command_result(action, ret);
}

static void handle_button_down_long_end(void) {
  // stop_walk_mode("DOWN release: stop walk mode");
}

// static void start_cycleiq_controller(boot_mode_t mode) {
//   if (mode == BOOT_MODE_CONFIG) {
//     ESP_LOGW(TAG, "Configuration boot selected; starting CycleIQ normally");
//   }
//
//   (void)request_controller_power(true);
// }

static void nvs_init(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
}

static void log_init_error(const char *component, esp_err_t ret) {
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "%s init failed: %s", component, esp_err_to_name(ret));
  }
}

static void peak_app_task(void *arg) {
  (void)arg;
  nvs_init();
  buttons_init();

  boot_mode_t mode = boot(handle_boot_mountain_mode);

  printf("Starting PEAK application in mode: %s\n",
         mode == BOOT_MODE_NORMAL ? "NORMAL" : "CONFIG");

  // ESP_ERROR_CHECK(display_start());

  i2c_master_init();
  ltr329_sensor_init(&bus_handle);
  t117_sensor_init(&bus_handle);

  battery_monitor_init();

  buttons_on(BTN_POWER, BTN_EVENT_LONG_PRESS_START, handle_button_power_long);
  buttons_on(BTN_POWER, BTN_EVENT_LONG_PRESS_END, handle_button_power_long_stop);

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  // Button event handlers
  // buttons_on(BTN_UP, BTN_EVENT_CLICK, button_up_pressed);
  // buttons_on(BTN_UP, BTN_EVENT_LONG_PRESS_START, button_up_long_pressed);
  // ignore_boot_power_release =
  //     mode == BOOT_MODE_NORMAL && buttons_is_pressed(BTN_POWER);
  // buttons_on(BTN_POWER, BTN_EVENT_CLICK, button_power_pressed);
  // buttons_on(BTN_DOWN, BTN_EVENT_CLICK,
  // button_down_pressed); buttons_on(BTN_DOWN, BTN_EVENT_LONG_PRESS_START,
  // button_down_long_started); buttons_on(BTN_DOWN, BTN_EVENT_LONG_PRESS_END,
  // button_down_long_ended);
  //
  // for (;;) {
  //   uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  //
  //   peak_button_event_t event;
  //   now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  //   if (xQueueReceive(button_event_queue, &event,
  //                     walk_event_wait_ticks(now_ms)) == pdTRUE) {
  //     handle_button_event(event);
  //   }
  // }
}

void app_main(void) {
  BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
      peak_app_task, "peak_app", PEAK_APP_TASK_STACK_SIZE, NULL,
      ESP_TASK_MAIN_PRIO, NULL, ESP_TASK_MAIN_CORE,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);

  ESP_ERROR_CHECK(ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
