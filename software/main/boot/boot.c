#include "boot.h"
#include "buttons.h"
#include "power.h"
#include "utils/time.h"
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POWER_HOLD_TIME_MS 1000U
#define MOUNTAIN_MODE_HOLD_TIME_MS 3000U

static const char *TAG = "PEAK";

static void mountain_mode_task(void *arg) {
  mountain_mode_callback_t cb = (mountain_mode_callback_t)arg;
  uint32_t start_time = millis();
  bool mountain_mode_active = true;

  while ((uint32_t)(millis() - start_time) < MOUNTAIN_MODE_HOLD_TIME_MS) {
    if (!buttons_is_pressed(BTN_POWER) || !buttons_is_pressed(BTN_UP)) {
      mountain_mode_active = false;
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (mountain_mode_active && cb != NULL) {
    cb();
  }

  while (buttons_is_pressed(BTN_UP)) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // POWER began this boot gesture, so its later release is not a shutdown.
  buttons_ignore_until_released(BTN_POWER);
  buttons_resume(BTN_POWER, BTN_EVENT_ALL);
  buttons_resume(BTN_UP, BTN_EVENT_ALL);

  vTaskDelete(NULL);
}

boot_mode_t boot(mountain_mode_callback_t cb) {
  uint32_t boot_time = millis();

  while ((uint32_t)(millis() - boot_time) < POWER_HOLD_TIME_MS) {
    if (!buttons_is_pressed(BTN_POWER)) {
      power_enter_deep_sleep();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  bool up_pressed = buttons_is_pressed(BTN_UP);
  bool power_pressed = buttons_is_pressed(BTN_POWER);
  bool down_pressed = buttons_is_pressed(BTN_DOWN);

  if (power_pressed && down_pressed) {
    buttons_ignore_until_released(BTN_POWER);
    return BOOT_MODE_CONFIG;
  }

  if (!power_pressed || !up_pressed) {
    if (power_pressed) {
      buttons_ignore_until_released(BTN_POWER);
    }
    return BOOT_MODE_NORMAL;
  }

  buttons_pause(BTN_POWER, BTN_EVENT_ALL);
  buttons_pause(BTN_UP, BTN_EVENT_ALL);
  BaseType_t task_created =
      xTaskCreate(mountain_mode_task, "mountain_mode_task", 2048, cb,
                  tskIDLE_PRIORITY + 1, NULL);
  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "Failed to start mountain mode task");
    buttons_ignore_until_released(BTN_POWER);
    buttons_resume(BTN_POWER, BTN_EVENT_ALL);
    buttons_resume(BTN_UP, BTN_EVENT_ALL);
  }

  return BOOT_MODE_NORMAL;
}
