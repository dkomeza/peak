#include "boot.h"
#include "buttons.h"
#include "utils/time.h"
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BOOT_TIME 700
#define POWER_BTN_WINDOW 500
#define MOUNTAIN_MODE_HOLD_TIME 3000

static const char *TAG = "PEAK";

void mountain_mode_task(void *arg) {
  uint32_t start_time = millis();
  bool mountain_mode_active = false;

  while (buttons_is_pressed(BTN_POWER)) {
    if (millis() > start_time + POWER_BTN_WINDOW) {
      ESP_LOGI(TAG, "Power button held too long. Cancelling mountain mode.");
      vTaskDelete(NULL);
      return;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  for (;;) {
    if (start_time + MOUNTAIN_MODE_HOLD_TIME < millis()) {
      mountain_mode_active = true;
      break;
    }

    if (!buttons_is_pressed(BTN_UP)) {
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (mountain_mode_active) {
    ESP_LOGI(TAG, "Mountain mode activated!");
  } else {
    ESP_LOGI(TAG, "Mountain mode not activated.");
  }

  while (buttons_is_pressed(BTN_UP)) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  vTaskDelete(NULL);
}

boot_mode_t boot() {
  uint32_t boot_time = millis();

  if (!buttons_is_pressed(BTN_POWER)) {
    return BOOT_MODE_UNDETERMINED;
  }

  while (millis() < boot_time + BOOT_TIME) {
    if (!buttons_is_pressed(BTN_POWER)) {
      return BOOT_MODE_UNDETERMINED;
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Poll every 10 ms
  }

  bool up_pressed = buttons_is_pressed(BTN_UP);
  bool power_pressed = buttons_is_pressed(BTN_POWER);
  bool down_pressed = buttons_is_pressed(BTN_DOWN);

  if (!power_pressed) {
    return BOOT_MODE_UNDETERMINED;
  }

  if (up_pressed && down_pressed) {
    return BOOT_MODE_NORMAL;
  } else if (down_pressed) {
    return BOOT_MODE_CONFIG;
  } else if (!up_pressed) {
    return BOOT_MODE_NORMAL;
  }

  buttons_pause(BTN_POWER, BTN_EVENT_ALL);
  buttons_pause(BTN_UP, BTN_EVENT_ALL);
  xTaskCreate(mountain_mode_task, "Mountain BOOT task", 4096, NULL, 1, NULL);

  return BOOT_MODE_UNDETERMINED;
}
