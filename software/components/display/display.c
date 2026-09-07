#include "display/display.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "port/backlight.h"
#include "port/display_port.h"
#include "ui/home.h"

#define DISPLAY_UI_TASK_STACK_SIZE 6144
#define DISPLAY_UI_TASK_PRIORITY 4
#define DISPLAY_INIT_TIMEOUT_MS 5000
#define DISPLAY_SLEEP_TIMEOUT_MS 200
#define DISPLAY_MAX_WAIT_MS 20

typedef struct {
  portMUX_TYPE lock;
  display_state_t latest_state;
  bool state_pending;
  bool sleep_pending;
  StaticSemaphore_t init_done_storage;
  SemaphoreHandle_t init_done;
  StaticSemaphore_t sleep_done_storage;
  SemaphoreHandle_t sleep_done;
  StaticSemaphore_t sleep_mutex_storage;
  SemaphoreHandle_t sleep_mutex;
  TaskHandle_t task;
  esp_err_t init_result;
  esp_err_t sleep_result;
  bool started;
  bool ready;
} display_runtime_t;

static const char *TAG = "display";
static display_runtime_t s_runtime = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

static TickType_t wait_ticks(uint32_t delay_ms) {
  uint32_t bounded_delay_ms =
      delay_ms > DISPLAY_MAX_WAIT_MS ? DISPLAY_MAX_WAIT_MS : delay_ms;
  return pdMS_TO_TICKS(bounded_delay_ms);
}

static bool process_pending_work(display_home_t *home) {
  display_state_t state;
  bool state_pending;
  bool sleep_pending;

  portENTER_CRITICAL(&s_runtime.lock);
  state = s_runtime.latest_state;
  state_pending = s_runtime.state_pending;
  s_runtime.state_pending = false;
  sleep_pending = s_runtime.sleep_pending;
  s_runtime.sleep_pending = false;
  portEXIT_CRITICAL(&s_runtime.lock);

  if (sleep_pending) {
    s_runtime.sleep_result = display_port_sleep();
    xSemaphoreGive(s_runtime.sleep_done);
    return true;
  }
  if (state_pending) {
    display_home_update(home, &state);
  }
  return false;
}

static void display_ui_task(void *arg) {
  (void)arg;
  display_home_t home = {0};
  s_runtime.init_result = display_port_init();
  if (s_runtime.init_result == ESP_OK) {
    display_home_create(&home);

    s_runtime.init_result = backlight_set_percent(100);
    if (s_runtime.init_result == ESP_OK) {
      s_runtime.init_result = backlight_set_enabled(true);
    }
  }

  if (s_runtime.init_result == ESP_OK) {
    portENTER_CRITICAL(&s_runtime.lock);
    s_runtime.ready = true;
    portEXIT_CRITICAL(&s_runtime.lock);
    xSemaphoreGive(s_runtime.init_done);

    for (;;) {
      if (process_pending_work(&home)) {
        vTaskDelete(NULL);
        return;
      }
      uint32_t delay_ms = display_port_timer_handler();
      ulTaskNotifyTake(pdTRUE, wait_ticks(delay_ms));
    }
  }
  xSemaphoreGive(s_runtime.init_done);

  ESP_LOGE(TAG, "Display initialization failed: %s",
           esp_err_to_name(s_runtime.init_result));
  vTaskDelete(NULL);
}

esp_err_t display_start(void) {
  if (s_runtime.started) {
    return ESP_ERR_INVALID_STATE;
  }

  s_runtime.init_done =
      xSemaphoreCreateBinaryStatic(&s_runtime.init_done_storage);
  s_runtime.sleep_done =
      xSemaphoreCreateBinaryStatic(&s_runtime.sleep_done_storage);
  s_runtime.sleep_mutex =
      xSemaphoreCreateMutexStatic(&s_runtime.sleep_mutex_storage);
  if (s_runtime.init_done == NULL || s_runtime.sleep_done == NULL ||
      s_runtime.sleep_mutex == NULL) {
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

  return s_runtime.init_result;
}

esp_err_t display_update(const display_state_t *state) {
  if (state == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  TaskHandle_t task;
  portENTER_CRITICAL(&s_runtime.lock);
  if (!s_runtime.ready) {
    portEXIT_CRITICAL(&s_runtime.lock);
    return ESP_ERR_INVALID_STATE;
  }
  s_runtime.latest_state = *state;
  s_runtime.state_pending = true;
  task = s_runtime.task;
  portEXIT_CRITICAL(&s_runtime.lock);

  xTaskNotifyGive(task);
  return ESP_OK;
}

esp_err_t display_sleep(void) {
  TaskHandle_t task;
  portENTER_CRITICAL(&s_runtime.lock);
  if (!s_runtime.ready) {
    portEXIT_CRITICAL(&s_runtime.lock);
    return ESP_ERR_INVALID_STATE;
  }
  task = s_runtime.task;
  portEXIT_CRITICAL(&s_runtime.lock);

  if (xTaskGetCurrentTaskHandle() == task) {
    return ESP_ERR_INVALID_STATE;
  }
  if (xSemaphoreTake(s_runtime.sleep_mutex,
                     pdMS_TO_TICKS(DISPLAY_SLEEP_TIMEOUT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  xSemaphoreTake(s_runtime.sleep_done, 0);

  portENTER_CRITICAL(&s_runtime.lock);
  s_runtime.ready = false;
  s_runtime.sleep_pending = true;
  portEXIT_CRITICAL(&s_runtime.lock);
  xTaskNotifyGive(task);

  if (xSemaphoreTake(s_runtime.sleep_done,
                     pdMS_TO_TICKS(DISPLAY_SLEEP_TIMEOUT_MS)) != pdTRUE) {
    xSemaphoreGive(s_runtime.sleep_mutex);
    return ESP_ERR_TIMEOUT;
  }
  esp_err_t result = s_runtime.sleep_result;
  xSemaphoreGive(s_runtime.sleep_mutex);
  return result;
}
