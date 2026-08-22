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
#define PEAK_WALK_REFRESH_MS 250U
#define PEAK_WALK_ERROR_LOG_INTERVAL_MS 1000U
#define PEAK_BOOT_COUNT_MAGIC 0x5045414bU
#define PEAK_BOOT_DIAGNOSTIC_MS 15000U

typedef enum {
  PEAK_STAGE_UNKNOWN = 0,
  PEAK_STAGE_APP_START,
  PEAK_STAGE_NVS,
  PEAK_STAGE_BUTTONS,
  PEAK_STAGE_CAN,
  PEAK_STAGE_ESC,
  PEAK_STAGE_WIFI,
  PEAK_STAGE_VESC,
  PEAK_STAGE_BLE_OTA,
  PEAK_STAGE_IO,
  PEAK_STAGE_DISPLAY,
  PEAK_STAGE_RUNNING,
} peak_boot_stage_t;

typedef enum {
  PEAK_BUTTON_EVENT_UP_CLICK,
  PEAK_BUTTON_EVENT_POWER_CLICK,
  PEAK_BUTTON_EVENT_DOWN_CLICK,
  PEAK_BUTTON_EVENT_UP_LONG,
  PEAK_BUTTON_EVENT_POWER_LONG,
  PEAK_BUTTON_EVENT_DOWN_LONG_START,
  PEAK_BUTTON_EVENT_DOWN_LONG_END,
  PEAK_BUTTON_EVENT_BOOT_MOUNTAIN,
} peak_button_event_t;

static const char *TAG = "peak_app";
static i2c_master_bus_handle_t bus_handle;
static QueueHandle_t button_event_queue;
static esc_controller_t peak_controller;
static uint8_t current_gear = PEAK_MIN_GEAR;
static esc_ride_mode_t current_ride_mode = ESC_RIDE_MODE_NORMAL;
static esc_support_mode_t current_support_mode = ESC_SUPPORT_MODE_PAS;
static bool walk_command_active;
static uint32_t next_walk_refresh_ms;
static uint32_t last_walk_refresh_error_ms;
static RTC_NOINIT_ATTR uint32_t boot_count_magic;
static RTC_NOINIT_ATTR uint32_t boot_count;
static RTC_NOINIT_ATTR uint32_t last_boot_stage;

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

static void publish_display_event(const display_event_t *event) {
  esp_err_t ret = display_event_publish(event);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Display event dropped: %s", esp_err_to_name(ret));
  }
}

static void publish_control_state(void) {
  display_event_t event = {
      .type = DISPLAY_EVENT_CONTROL_STATE,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .data.control =
          {
              .gear = current_gear,
              .support_mode = (uint8_t)current_support_mode,
              .ride_mode = (uint8_t)current_ride_mode,
              .walk_active = walk_command_active,
          },
  };
  publish_display_event(&event);
}

static void publish_action_result(display_action_t action, esp_err_t result) {
  display_event_t event = {
      .type = DISPLAY_EVENT_ACTION_RESULT,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .data.action =
          {
              .action = action,
              .result = result,
          },
  };
  publish_display_event(&event);
  if (result == ESP_OK) {
    publish_control_state();
  }
}

static void publish_boot_stage(peak_boot_stage_t stage) {
  display_event_t event = {
      .type = DISPLAY_EVENT_BOOT_STAGE,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .data.boot =
          {
              .stage = (uint8_t)stage,
          },
  };
  publish_display_event(&event);
}

static void queue_button_event(peak_button_event_t event) {
  if (button_event_queue == NULL) {
    return;
  }

  if (xQueueSend(button_event_queue, &event, 0) != pdTRUE) {
    ESP_LOGW(TAG, "button event queue full; dropped event %d", (int)event);
  }
}

void button_up_pressed(void) { queue_button_event(PEAK_BUTTON_EVENT_UP_CLICK); }

void button_power_pressed(void) {
  queue_button_event(PEAK_BUTTON_EVENT_POWER_CLICK);
}

void button_down_pressed(void) {
  queue_button_event(PEAK_BUTTON_EVENT_DOWN_CLICK);
}

void button_up_long_pressed(void) {
  queue_button_event(PEAK_BUTTON_EVENT_UP_LONG);
}

void button_power_long_pressed(void) {
  queue_button_event(PEAK_BUTTON_EVENT_POWER_LONG);
}

void button_down_long_started(void) {
  queue_button_event(PEAK_BUTTON_EVENT_DOWN_LONG_START);
}

void button_down_long_ended(void) {
  queue_button_event(PEAK_BUTTON_EVENT_DOWN_LONG_END);
}

static void handle_button_up_click(void) {
  uint8_t next_gear =
      current_gear < PEAK_MAX_GEAR ? current_gear + 1 : current_gear;
  esp_err_t ret = esc_controller_set_gear(&peak_controller, next_gear);
  if (ret == ESP_OK) {
    current_gear = next_gear;
  }
  publish_action_result(DISPLAY_ACTION_GEAR, ret);
  log_esc_command_result("UP click: gear up", ret);
}

static void handle_button_power_click(void) { ESP_LOGI(TAG, "POWER click"); }

static void handle_button_down_click(void) {
  uint8_t next_gear =
      current_gear > PEAK_MIN_GEAR ? current_gear - 1 : current_gear;
  esp_err_t ret = esc_controller_set_gear(&peak_controller, next_gear);
  if (ret == ESP_OK) {
    current_gear = next_gear;
  }
  publish_action_result(DISPLAY_ACTION_GEAR, ret);
  log_esc_command_result("DOWN click: gear down", ret);
}

static void handle_button_up_long(void) {
  esc_support_mode_t next_mode = current_support_mode == ESC_SUPPORT_MODE_PAS
                                     ? ESC_SUPPORT_MODE_TORQUE
                                     : ESC_SUPPORT_MODE_PAS;

  esp_err_t ret = esc_controller_set_support_mode(&peak_controller, next_mode);
  if (ret == ESP_OK) {
    current_support_mode = next_mode;
  }
  publish_action_result(DISPLAY_ACTION_SUPPORT_MODE, ret);
  log_esc_command_result("UP long press: toggle support mode", ret);
}

static void handle_button_power_long(void) {
  esc_ride_mode_t next_mode = current_ride_mode == ESC_RIDE_MODE_NORMAL
                                  ? ESC_RIDE_MODE_MOUNTAIN
                                  : ESC_RIDE_MODE_NORMAL;

  esp_err_t ret = esc_controller_set_ride_mode(&peak_controller, next_mode);
  if (ret == ESP_OK) {
    current_ride_mode = next_mode;
  }
  publish_action_result(DISPLAY_ACTION_RIDE_MODE, ret);
  log_esc_command_result("POWER long press: toggle ride mode", ret);
}

static void handle_boot_mountain_mode(void) {
  esp_err_t ret =
      esc_controller_set_ride_mode(&peak_controller, ESC_RIDE_MODE_MOUNTAIN);
  if (ret == ESP_OK) {
    current_ride_mode = ESC_RIDE_MODE_MOUNTAIN;
  }
  publish_action_result(DISPLAY_ACTION_RIDE_MODE, ret);
  log_esc_command_result("Mountain boot mode", ret);
}

static void handle_button_down_long_start(void) {
  if (walk_command_active) {
    return;
  }

  walk_command_active = true;
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  esp_err_t ret = esc_controller_set_walk_mode(&peak_controller, true);
  next_walk_refresh_ms = now_ms + PEAK_WALK_REFRESH_MS;
  last_walk_refresh_error_ms = ret == ESP_OK ? 0 : now_ms;

  publish_action_result(DISPLAY_ACTION_WALK_MODE, ret);
  log_esc_command_result("DOWN long press: start walk mode", ret);
}

static void stop_walk_mode(const char *action) {
  if (!walk_command_active) {
    return;
  }

  walk_command_active = false;
  esp_err_t ret = esc_controller_set_walk_mode(&peak_controller, false);
  publish_action_result(DISPLAY_ACTION_WALK_MODE, ret);
  log_esc_command_result(action, ret);
}

static void handle_button_down_long_end(void) {
  stop_walk_mode("DOWN release: stop walk mode");
}

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms) {
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void service_walk_mode(uint32_t now_ms) {
  if (!walk_command_active) {
    return;
  }

  if (!buttons_is_pressed(BTN_DOWN)) {
    stop_walk_mode("DOWN release fallback: stop walk mode");
    return;
  }

  if (!time_reached(now_ms, next_walk_refresh_ms)) {
    return;
  }

  esp_err_t ret = esc_controller_set_walk_mode(&peak_controller, true);
  if (ret == ESP_OK) {
    last_walk_refresh_error_ms = 0;
  } else if (last_walk_refresh_error_ms == 0 ||
             now_ms - last_walk_refresh_error_ms >=
                 PEAK_WALK_ERROR_LOG_INTERVAL_MS) {
    ESP_LOGW(TAG, "walk mode refresh failed: %s", esp_err_to_name(ret));
    last_walk_refresh_error_ms = now_ms;
  }

  next_walk_refresh_ms = now_ms + PEAK_WALK_REFRESH_MS;
}

static TickType_t walk_event_wait_ticks(uint32_t now_ms) {
  if (!walk_command_active) {
    return portMAX_DELAY;
  }

  if (time_reached(now_ms, next_walk_refresh_ms)) {
    return 0;
  }

  TickType_t wait_ticks = pdMS_TO_TICKS(next_walk_refresh_ms - now_ms);
  return wait_ticks > 0 ? wait_ticks : 1;
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
  case PEAK_BUTTON_EVENT_POWER_LONG:
    handle_button_power_long();
    break;
  case PEAK_BUTTON_EVENT_DOWN_LONG_START:
    handle_button_down_long_start();
    break;
  case PEAK_BUTTON_EVENT_DOWN_LONG_END:
    handle_button_down_long_end();
    break;
  case PEAK_BUTTON_EVENT_BOOT_MOUNTAIN:
    handle_boot_mountain_mode();
    break;
  default:
    break;
  }
}

/**
 * Called when the device is booted into mountain mode. This will be used
 * to send appropriate commands to the ESC and change the UI
 */
static void mountain_mode_callback(void) {
  queue_button_event(PEAK_BUTTON_EVENT_BOOT_MOUNTAIN);
}

static void start_cycleiq_controller(boot_mode_t mode) {
  if (mode == BOOT_MODE_CONFIG) {
    ESP_LOGW(TAG, "Configuration boot selected; starting CycleIQ normally");
  }

  esp_err_t ret = esc_controller_set_power(&peak_controller, true);
  publish_action_result(DISPLAY_ACTION_POWER, ret);
  log_esc_command_result("CycleIQ power on", ret);
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

static void log_init_error(const char *component, esp_err_t ret) {
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "%s init failed: %s", component, esp_err_to_name(ret));
  }
}

static const char *reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON:
    return "POWERON";
  case ESP_RST_EXT:
    return "EXT";
  case ESP_RST_SW:
    return "SW";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INT WDT";
  case ESP_RST_TASK_WDT:
    return "TASK WDT";
  case ESP_RST_WDT:
    return "WDT";
  case ESP_RST_DEEPSLEEP:
    return "DEEPSLEEP";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_SDIO:
    return "SDIO";
  case ESP_RST_USB:
    return "USB";
  case ESP_RST_JTAG:
    return "JTAG";
  case ESP_RST_EFUSE:
    return "EFUSE";
  case ESP_RST_UNKNOWN:
  default:
    return "UNKNOWN";
  }
}

static const char *boot_stage_name(uint32_t stage) {
  switch ((peak_boot_stage_t)stage) {
  case PEAK_STAGE_APP_START:
    return "app start";
  case PEAK_STAGE_NVS:
    return "nvs";
  case PEAK_STAGE_BUTTONS:
    return "buttons";
  case PEAK_STAGE_CAN:
    return "can";
  case PEAK_STAGE_ESC:
    return "esc";
  case PEAK_STAGE_WIFI:
    return "wifi";
  case PEAK_STAGE_VESC:
    return "vesc";
  case PEAK_STAGE_BLE_OTA:
    return "ble ota";
  case PEAK_STAGE_IO:
    return "io";
  case PEAK_STAGE_DISPLAY:
    return "display";
  case PEAK_STAGE_RUNNING:
    return "running";
  case PEAK_STAGE_UNKNOWN:
  default:
    return "unknown";
  }
}

static void set_boot_stage(peak_boot_stage_t stage) {
  last_boot_stage = (uint32_t)stage;
}

static void log_reset_context(void) {
  if (boot_count_magic != PEAK_BOOT_COUNT_MAGIC) {
    boot_count_magic = PEAK_BOOT_COUNT_MAGIC;
    boot_count = 0;
    last_boot_stage = PEAK_STAGE_UNKNOWN;
  }
  boot_count++;
  ESP_LOGW(TAG,
           "Boot #%" PRIu32 ", reset reason=%s(%d), last stage=%s, free "
           "heap=%" PRIu32,
           boot_count, reset_reason_name(esp_reset_reason()),
           esp_reset_reason(), boot_stage_name(last_boot_stage),
           (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

static void peak_app_task(void *arg) {
  (void)arg;
  log_reset_context();
  set_boot_stage(PEAK_STAGE_APP_START);

  set_boot_stage(PEAK_STAGE_NVS);
  nvs_init();

  set_boot_stage(PEAK_STAGE_BUTTONS);
  button_event_queue =
      xQueueCreate(PEAK_BUTTON_EVENT_QUEUE_LEN, sizeof(peak_button_event_t));
  ESP_ERROR_CHECK(button_event_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

  buttons_init();

  boot_mode_t mode = boot(mountain_mode_callback);

  set_boot_stage(PEAK_STAGE_DISPLAY);
  ESP_ERROR_CHECK(display_start());

  // ESP_ERROR_CHECK(wifi_start("DEKANET", "tramwaj55"));
  set_boot_stage(PEAK_STAGE_CAN);
  ESP_ERROR_CHECK(can_init());
  set_boot_stage(PEAK_STAGE_ESC);
  ESP_ERROR_CHECK(display_event_adapter_start());
  esc_peak_init();
  ESP_ERROR_CHECK(esc_peak_controller_init(&peak_controller));
  start_cycleiq_controller(mode);

  set_boot_stage(PEAK_STAGE_WIFI);
  log_init_error("Wi-Fi AP", wifi_start_ap());

  set_boot_stage(PEAK_STAGE_VESC);
  ESP_ERROR_CHECK(vesc_bridge_init());
  const transport_iface_t *vesc_transports[] = {
      &transport_udp,
      &transport_ble,
  };
  log_init_error(
      "VESC bridge",
      vesc_bridge_start(vesc_transports,
                        sizeof(vesc_transports) / sizeof(vesc_transports[0])));
  set_boot_stage(PEAK_STAGE_BLE_OTA);
  log_init_error("BLE OTA", ble_ota_start());

  // IO initialization
  set_boot_stage(PEAK_STAGE_IO);
  i2c_master_init();
  ltr329_sensor_init(&bus_handle);
  t117_sensor_init(&bus_handle);
  battery_monitor_init();

  // Button event handlers
  buttons_on(BTN_UP, BTN_EVENT_CLICK, button_up_pressed);
  buttons_on(BTN_UP, BTN_EVENT_LONG_PRESS_START, button_up_long_pressed);
  buttons_on(BTN_POWER, BTN_EVENT_CLICK, button_power_pressed);
  buttons_on(BTN_POWER, BTN_EVENT_LONG_PRESS_START, button_power_long_pressed);
  buttons_on(BTN_DOWN, BTN_EVENT_CLICK, button_down_pressed);
  buttons_on(BTN_DOWN, BTN_EVENT_LONG_PRESS_START, button_down_long_started);
  buttons_on(BTN_DOWN, BTN_EVENT_LONG_PRESS_END, button_down_long_ended);

  set_boot_stage(PEAK_STAGE_RUNNING);
  publish_boot_stage(PEAK_STAGE_RUNNING);
  publish_control_state();

  for (;;) {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    service_walk_mode(now_ms);

    peak_button_event_t event;
    now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (xQueueReceive(button_event_queue, &event,
                      walk_event_wait_ticks(now_ms)) == pdTRUE) {
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
