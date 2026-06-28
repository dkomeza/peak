#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
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

#include "display/display.h"

#include "esc/peak.h"

#include "vesc/vesc_bridge.h"

#include "wireless/ble_bridge.h"
#include "wireless/ble_ota.h"
#include "wireless/udp_bridge.h"
#include "wireless/wifi.h"

#define PEAK_APP_TASK_STACK_SIZE 8192
#define PEAK_BUTTON_EVENT_QUEUE_LEN 8
#define PEAK_MIN_GEAR 0
#define PEAK_MAX_GEAR 6

typedef enum {
  PEAK_BUTTON_EVENT_UP_CLICK,
  PEAK_BUTTON_EVENT_POWER_CLICK,
  PEAK_BUTTON_EVENT_DOWN_CLICK,
  PEAK_BUTTON_EVENT_UP_LONG,
  PEAK_BUTTON_EVENT_DOWN_LONG,
} peak_button_event_t;

static const char *TAG = "peak_app";
static i2c_master_bus_handle_t bus_handle;
static QueueHandle_t button_event_queue;
static esc_controller_t peak_controller;
static uint8_t current_gear = PEAK_MIN_GEAR;
static esc_ride_mode_t current_ride_mode = ESC_RIDE_MODE_NORMAL;
static esc_support_mode_t current_support_mode = ESC_SUPPORT_MODE_PAS;

void i2c_master_init() {
  i2c_master_bus_config_t bus_config = {.sda_io_num = 31,
                                        .scl_io_num = 30,
                                        .clk_source = I2C_CLK_SRC_DEFAULT,
                                        .flags = {
                                            .enable_internal_pullup = false,
                                        }};
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
}

static void log_esc_command_result(const char *action, esp_err_t ret) {
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "%s", action);
  } else {
    ESP_LOGW(TAG, "%s failed: %s", action, esp_err_to_name(ret));
  }
}

static void queue_button_event(peak_button_event_t event) {
  if (button_event_queue == NULL) {
    return;
  }

  (void)xQueueSend(button_event_queue, &event, 0);
}

void button_up_pressed(void) {
  queue_button_event(PEAK_BUTTON_EVENT_UP_CLICK);
}

void button_power_pressed(void) {
  queue_button_event(PEAK_BUTTON_EVENT_POWER_CLICK);
}

void button_down_pressed(void) {
  queue_button_event(PEAK_BUTTON_EVENT_DOWN_CLICK);
}

void button_up_long_pressed(void) {
  queue_button_event(PEAK_BUTTON_EVENT_UP_LONG);
}

void button_down_long_pressed(void) {
  queue_button_event(PEAK_BUTTON_EVENT_DOWN_LONG);
}

static void handle_button_up_click(void) {
  uint8_t next_gear = current_gear < PEAK_MAX_GEAR ? current_gear + 1
                                                   : current_gear;
  esp_err_t ret = esc_controller_set_gear(&peak_controller, next_gear);
  if (ret == ESP_OK) {
    current_gear = next_gear;
  }
  display_show_button_event(DISPLAY_BUTTON_EVENT_UP, ret != ESP_OK);
  log_esc_command_result("UP click: gear up", ret);
}

static void handle_button_power_click(void) {
  display_show_button_event(DISPLAY_BUTTON_EVENT_POWER, false);
  ESP_LOGI(TAG, "POWER click");
}

static void handle_button_down_click(void) {
  uint8_t next_gear = current_gear > PEAK_MIN_GEAR ? current_gear - 1
                                                   : current_gear;
  esp_err_t ret = esc_controller_set_gear(&peak_controller, next_gear);
  if (ret == ESP_OK) {
    current_gear = next_gear;
  }
  display_show_button_event(DISPLAY_BUTTON_EVENT_DOWN, ret != ESP_OK);
  log_esc_command_result("DOWN click: gear down", ret);
}

static void handle_button_up_long(void) {
  esc_ride_mode_t next_mode = current_ride_mode == ESC_RIDE_MODE_NORMAL
                                  ? ESC_RIDE_MODE_MOUNTAIN
                                  : ESC_RIDE_MODE_NORMAL;

  esp_err_t ret = esc_controller_set_ride_mode(&peak_controller, next_mode);
  if (ret == ESP_OK) {
    current_ride_mode = next_mode;
  }
  display_show_button_event(DISPLAY_BUTTON_EVENT_UP, ret != ESP_OK);
  log_esc_command_result("UP long press: toggle ride mode", ret);
}

static void handle_button_down_long(void) {
  esc_support_mode_t next_mode = current_support_mode == ESC_SUPPORT_MODE_PAS
                                     ? ESC_SUPPORT_MODE_TORQUE
                                     : ESC_SUPPORT_MODE_PAS;

  esp_err_t ret = esc_controller_set_support_mode(&peak_controller, next_mode);
  if (ret == ESP_OK) {
    current_support_mode = next_mode;
  }
  display_show_button_event(DISPLAY_BUTTON_EVENT_DOWN, ret != ESP_OK);
  log_esc_command_result("DOWN long press: toggle support mode", ret);
}

static void handle_button_event(peak_button_event_t event) {
  switch (event) {
  case PEAK_BUTTON_EVENT_UP_CLICK:
    handle_button_up_click();
    break;
  case PEAK_BUTTON_EVENT_POWER_CLICK:
    handle_button_power_click();
    break;
  case PEAK_BUTTON_EVENT_DOWN_CLICK:
    handle_button_down_click();
    break;
  case PEAK_BUTTON_EVENT_UP_LONG:
    handle_button_up_long();
    break;
  case PEAK_BUTTON_EVENT_DOWN_LONG:
    handle_button_down_long();
    break;
  default:
    break;
  }
}

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

  button_event_queue =
      xQueueCreate(PEAK_BUTTON_EVENT_QUEUE_LEN, sizeof(peak_button_event_t));
  ESP_ERROR_CHECK(button_event_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

  buttons_init();

  // boot_mode_t mode = boot(mountain_mode_callback);

  // ESP_ERROR_CHECK(wifi_start("DEKANET", "tramwaj55"));
  ESP_ERROR_CHECK(can_init());
  esc_peak_init();
  ESP_ERROR_CHECK(esc_peak_controller_init(&peak_controller));

  ESP_ERROR_CHECK(wifi_start_ap());

  ESP_ERROR_CHECK(vesc_bridge_init());
  const transport_iface_t *vesc_transports[] = {
      &transport_udp,
      &transport_ble,
  };
  ESP_ERROR_CHECK(
      vesc_bridge_start(vesc_transports, sizeof(vesc_transports) /
                                             sizeof(vesc_transports[0])));
  ESP_ERROR_CHECK(ble_ota_start());

  // IO initialization
  i2c_master_init();
  ltr329_sensor_init(&bus_handle);
  t117_sensor_init(&bus_handle);
  battery_monitor_init();

  // Button event handlers
  buttons_on(BTN_UP, BTN_EVENT_CLICK, button_up_pressed);
  buttons_on(BTN_UP, BTN_EVENT_LONG_PRESS_START, button_up_long_pressed);
  buttons_on(BTN_POWER, BTN_EVENT_CLICK, button_power_pressed);
  buttons_on(BTN_DOWN, BTN_EVENT_CLICK, button_down_pressed);
  buttons_on(BTN_DOWN, BTN_EVENT_LONG_PRESS_START, button_down_long_pressed);

  ESP_ERROR_CHECK(display_init());

  for (;;) {
    peak_button_event_t event;
    if (xQueueReceive(button_event_queue, &event, pdMS_TO_TICKS(1000)) ==
        pdTRUE) {
      handle_button_event(event);
    }
  }
}

void app_main(void) {
  BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
      peak_app_task, "peak_app", PEAK_APP_TASK_STACK_SIZE, NULL,
      ESP_TASK_MAIN_PRIO, NULL, ESP_TASK_MAIN_CORE,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);

  ESP_ERROR_CHECK(ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
