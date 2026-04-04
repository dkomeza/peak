#include "include/button.h"
#include "hal/gpio_types.h"

#include <driver/gpio.h>
#include <esp_timer.h>
#include <stdlib.h>

#define DEBOUNCE_TIME_MS 20     // Time to wait for signal to stabilize
#define LONG_PRESS_TIME_MS 1000 // 1 second for a long press

button_state_t *button_init(int pin) {
  button_state_t *btn = calloc(1, sizeof(button_state_t));
  btn->pin = pin;

  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = (1ULL << pin),
      .pull_down_en = false,
      .pull_up_en = false,
      .hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE,
  };

  esp_err_t err = gpio_config(&io_conf);
  ESP_ERROR_CHECK(err);

  return btn;
}

void button_attach_callback(btn_event_type_t event_type, callback_t callback,
                            volatile button_state_t *btn) {
  if (!btn)
    return;

  switch (event_type) {
  case BTN_EVENT_DOWN:
    btn->on_down = callback;
    break;
  case BTN_EVENT_UP:
    btn->on_up = callback;
    break;
  case BTN_EVENT_CLICK:
    btn->on_click = callback;
    break;
  case BTN_EVENT_LONG_PRESS_START:
    btn->on_long_press_start = callback;
    break;
  case BTN_EVENT_LONG_PRESS_END:
    btn->on_long_press_end = callback;
    break;
  default:
    break;
  }
}

void button_detach_callback(btn_event_type_t event_type,
                            volatile button_state_t *btn) {
  if (!btn)
    return;

  if (event_type == BTN_EVENT_ALL) {
    btn->on_down = NULL;
    btn->on_up = NULL;
    btn->on_click = NULL;
    btn->on_long_press_start = NULL;
    btn->on_long_press_end = NULL;
  } else {
    switch (event_type) {
    case BTN_EVENT_DOWN:
      btn->on_down = NULL;
      break;
    case BTN_EVENT_UP:
      btn->on_up = NULL;
      break;
    case BTN_EVENT_CLICK:
      btn->on_click = NULL;
      break;
    case BTN_EVENT_LONG_PRESS_START:
      btn->on_long_press_start = NULL;
      break;
    case BTN_EVENT_LONG_PRESS_END:
      btn->on_long_press_end = NULL;
      break;
    default:
      break;
    }
  }
}

void button_update(volatile button_state_t *btn) {
  if (!btn)
    return;

  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

  bool is_pressed = (gpio_get_level(btn->pin) == 0);

  switch (btn->state) {

  case BTN_STATE_IDLE:
    if (is_pressed) {
      btn->state = BTN_STATE_DEBOUNCE;
      btn->state_time_ms = now_ms;
    }
    break;

  case BTN_STATE_DEBOUNCE:
    if (!is_pressed) {
      btn->state = BTN_STATE_IDLE;
    } else if ((now_ms - btn->state_time_ms) >= DEBOUNCE_TIME_MS) {
      btn->state = BTN_STATE_PRESS;
      btn->state_time_ms = now_ms;

      if (btn->on_down)
        btn->on_down();
    }
    break;

  case BTN_STATE_PRESS:
    if (!is_pressed) {
      btn->state = BTN_STATE_IDLE;

      if (btn->on_up)
        btn->on_up();
      if (btn->on_click)
        btn->on_click();

    } else if ((now_ms - btn->state_time_ms) >= LONG_PRESS_TIME_MS) {
      btn->state = BTN_STATE_LONG_PRESS;

      if (btn->on_long_press_start)
        btn->on_long_press_start();
    }
    break;

  case BTN_STATE_LONG_PRESS:
    if (!is_pressed) {
      btn->state = BTN_STATE_IDLE;

      if (btn->on_up)
        btn->on_up();
      if (btn->on_long_press_end)
        btn->on_long_press_end();
    }
    break;

  default:
    btn->state = BTN_STATE_IDLE;
    break;
  }
}
