#include "display/display.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "port/backlight.h"
#include "port/display_port.h"
#include "ui/home.h"

#define DISPLAY_UI_TASK_STACK_SIZE 6144
#define DISPLAY_UI_TASK_PRIORITY 4
#define DISPLAY_CRITICAL_QUEUE_LENGTH 8
#define DISPLAY_TELEMETRY_QUEUE_LENGTH 16
#define DISPLAY_INIT_TIMEOUT_MS 5000
#define DISPLAY_SLEEP_TIMEOUT_MS 200
#define DISPLAY_MAX_WAIT_MS 20

typedef struct {
  QueueHandle_t critical_queue;
  QueueHandle_t telemetry_queue;
  StaticQueue_t critical_queue_storage;
  StaticQueue_t telemetry_queue_storage;
  uint8_t critical_queue_data[DISPLAY_CRITICAL_QUEUE_LENGTH *
                              sizeof(display_event_t)];
  uint8_t telemetry_queue_data[DISPLAY_TELEMETRY_QUEUE_LENGTH *
                               sizeof(display_event_t)];
  StaticSemaphore_t init_done_storage;
  SemaphoreHandle_t init_done;
  StaticSemaphore_t sleep_done_storage;
  SemaphoreHandle_t sleep_done;
  TaskHandle_t task;
  esp_err_t init_result;
  esp_err_t sleep_result;
  bool started;
  bool ready;
} display_runtime_t;

static const char *TAG = "display";
static display_runtime_t s_runtime;

static bool is_critical_event(display_event_type_t type) {
  switch (type) {
  case DISPLAY_EVENT_BOOT_STAGE:
  case DISPLAY_EVENT_ACTION_RESULT:
  case DISPLAY_EVENT_CONTROL_STATE:
  case DISPLAY_EVENT_ESC_CONTROLLER_STATE:
  case DISPLAY_EVENT_ESC_WALK_STATE:
  case DISPLAY_EVENT_FAULT:
  case DISPLAY_EVENT_SLEEP:
    return true;
  default:
    return false;
  }
}

static void process_event(const display_event_t *event,
                          display_ui_model_t *model) {
  if (event->type == DISPLAY_EVENT_SLEEP) {
    s_runtime.sleep_result = display_port_sleep();
    xSemaphoreGive(s_runtime.sleep_done);
    return;
  }

  display_home_apply_event(event);
  if (display_ui_model_apply(model, event)) {
    display_home_update(model);
  }
}

static void process_queued_events(QueueHandle_t queue,
                                  display_ui_model_t *model) {
  display_event_t event;
  while (xQueueReceive(queue, &event, 0) == pdTRUE) {
    process_event(&event, model);
  }
}

static TickType_t wait_ticks(uint32_t delay_ms) {
  uint32_t bounded_delay_ms =
      delay_ms > DISPLAY_MAX_WAIT_MS ? DISPLAY_MAX_WAIT_MS : delay_ms;
  return pdMS_TO_TICKS(bounded_delay_ms);
}

static void display_ui_task(void *arg) {
  (void)arg;
  s_runtime.init_result = display_port_init();
  if (s_runtime.init_result == ESP_OK) {
    display_home_create();
    s_runtime.ready = true;
  }
  xSemaphoreGive(s_runtime.init_done);

  if (!s_runtime.ready) {
    ESP_LOGE(TAG, "Display initialization failed: %s",
             esp_err_to_name(s_runtime.init_result));
    vTaskDelete(NULL);
    return;
  }

  display_ui_model_t model = {0};
  for (;;) {
    process_queued_events(s_runtime.critical_queue, &model);
    process_queued_events(s_runtime.telemetry_queue, &model);

    uint32_t delay_ms = display_port_timer_handler();
    display_event_t event;
    if (xQueueReceive(s_runtime.critical_queue, &event, wait_ticks(delay_ms)) ==
        pdTRUE) {
      process_event(&event, &model);
    }
  }
}

esp_err_t display_start(void) {
  if (s_runtime.started) {
    return ESP_ERR_INVALID_STATE;
  }

  s_runtime.critical_queue = xQueueCreateStatic(
      DISPLAY_CRITICAL_QUEUE_LENGTH, sizeof(display_event_t),
      s_runtime.critical_queue_data, &s_runtime.critical_queue_storage);
  s_runtime.telemetry_queue = xQueueCreateStatic(
      DISPLAY_TELEMETRY_QUEUE_LENGTH, sizeof(display_event_t),
      s_runtime.telemetry_queue_data, &s_runtime.telemetry_queue_storage);
  s_runtime.init_done =
      xSemaphoreCreateBinaryStatic(&s_runtime.init_done_storage);
  s_runtime.sleep_done =
      xSemaphoreCreateBinaryStatic(&s_runtime.sleep_done_storage);
  if (s_runtime.critical_queue == NULL || s_runtime.telemetry_queue == NULL ||
      s_runtime.init_done == NULL || s_runtime.sleep_done == NULL) {
    return ESP_ERR_NO_MEM;
  }

  s_runtime.started = true;
  BaseType_t task_created = xTaskCreatePinnedToCore(
      display_ui_task, "display_ui", DISPLAY_UI_TASK_STACK_SIZE, NULL,
      DISPLAY_UI_TASK_PRIORITY, &s_runtime.task, tskNO_AFFINITY);
  if (task_created != pdPASS) {
    s_runtime.started = false;
    return ESP_ERR_NO_MEM;
  }

  if (xSemaphoreTake(s_runtime.init_done,
                     pdMS_TO_TICKS(DISPLAY_INIT_TIMEOUT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  backlight_set_percent(100);
  backlight_set_enabled(true);

  return s_runtime.init_result;
}

esp_err_t display_event_publish(const display_event_t *event) {
  if (event == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_runtime.ready) {
    return ESP_ERR_INVALID_STATE;
  }

  QueueHandle_t queue = is_critical_event(event->type)
                            ? s_runtime.critical_queue
                            : s_runtime.telemetry_queue;
  return xQueueSend(queue, event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t display_sleep(void) {
  backlight_set_enabled(false);
  if (!s_runtime.ready) {
    return ESP_ERR_INVALID_STATE;
  }

  display_event_t event = {.type = DISPLAY_EVENT_SLEEP};
  if (xQueueSend(s_runtime.critical_queue, &event, 0) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  if (xSemaphoreTake(s_runtime.sleep_done,
                     pdMS_TO_TICKS(DISPLAY_SLEEP_TIMEOUT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  return s_runtime.sleep_result;
}
