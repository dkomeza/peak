#include "buttons.h"
#include "button.h"
#include "freertos/idf_additions.h"
#include <stdbool.h>

#define BTN_UP_PIN 6
#define BTN_POWER_PIN 7
#define BTN_DOWN_PIN 8

static bool initialized = false;
volatile button_state_t *buttons[3];

void buttons_task(void *arg);

void buttons_init(void) {
  buttons[BTN_UP] = button_init(BTN_UP_PIN);
  buttons[BTN_POWER] = button_init(BTN_POWER_PIN);
  buttons[BTN_DOWN] = button_init(BTN_DOWN_PIN);

  initialized = true;
  xTaskCreatePinnedToCore(buttons_task, "Buttons Task", 4096, NULL, 1, NULL, 0);
}

void buttons_deinit(void) {
  if (!initialized)
    return;

  for (int i = 0; i < 3; i++) {
    button_detach_callback(BTN_EVENT_ALL, buttons[i]);
    free((void *)buttons[i]);
    buttons[i] = NULL;
  }

  initialized = false;
}

void buttons_task(void *arg) {
  if (!initialized) {
    vTaskDelete(NULL);
    return;
  }

  for (;;) {
    if (!initialized) {
      vTaskDelete(NULL);
      return;
    }

    for (int i = 0; i < 3; i++) {
      button_update(buttons[i]);
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Poll every 10 ms
  }
}

void buttons_on(button_t btn, btn_event_type_t event_type,
                callback_t callback) {
  if (!initialized || btn < 0 || btn >= 3)
    return;

  button_attach_callback(event_type, callback, buttons[btn]);
}

void buttons_clear(button_t btn, btn_event_type_t event_type) {

  if (!initialized || btn < 0 || btn >= 3)
    return;

  button_detach_callback(event_type, buttons[btn]);
}

bool buttons_is_pressed(button_t btn) {
  if (!initialized || btn < 0 || btn >= 3)
    return false;

  return button_is_pressed(buttons[btn]);
}

void buttons_pause(button_t btn, btn_event_type_t event_type) {
  if (!initialized || btn < 0 || btn >= 3)
    return;

  button_pause_callback(event_type, buttons[btn]);
}

void buttons_resume(button_t btn, btn_event_type_t event_type) {
  if (!initialized || btn < 0 || btn >= 3)
    return;

  button_resume_callback(event_type, buttons[btn]);
}
