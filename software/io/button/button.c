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

  int level = gpio_get_level(pin);
  btn->state = (level == 0) ? BTN_STATE_PRESS : BTN_STATE_IDLE;
  btn->state_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

  return btn;
}

volatile callback_t *event_type_to_callback(btn_event_type_t event_type,
                                            volatile button_state_t *btn) {
  switch (event_type) {
  case BTN_EVENT_DOWN:
    return &btn->on_down;
  case BTN_EVENT_UP:
    return &btn->on_up;
  case BTN_EVENT_CLICK:
    return &btn->on_click;
  case BTN_EVENT_LONG_PRESS_START:
    return &btn->on_long_press_start;
  case BTN_EVENT_LONG_PRESS_END:
    return &btn->on_long_press_end;
  default:
    return NULL;
  }
}

bool is_button_event_active(btn_event_type_t event_type,
                            volatile button_state_t *btn) {
  return (btn->pause_mask & event_type) == 0;
}

volatile callback_t *get_active_callback(btn_event_type_t event_type,
                                         volatile button_state_t *btn) {
  if (!is_button_event_active(event_type, btn))
    return NULL;

  return event_type_to_callback(event_type, btn);
}

bool button_is_pressed(volatile button_state_t *btn) {
  if (!btn)
    return false;

  return btn->state == BTN_STATE_PRESS || btn->state == BTN_STATE_LONG_PRESS;
}

void button_attach_callback(btn_event_type_t event_type, callback_t callback,
                            volatile button_state_t *btn) {
  if (!btn)
    return;

  volatile callback_t *cb_ptr = event_type_to_callback(event_type, btn);
  if (cb_ptr)
    *cb_ptr = callback;
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
    volatile callback_t *cb_ptr = event_type_to_callback(event_type, btn);
    if (cb_ptr)
      *cb_ptr = NULL;
  }
}

void button_pause_callback(btn_event_type_t event_type,
                           volatile button_state_t *btn) {
  if (!btn)
    return;

  btn->pause_mask |= event_type;
}

void button_resume_callback(btn_event_type_t event_type,
                            volatile button_state_t *btn) {
  if (!btn)
    return;

  btn->pause_mask &= (~event_type) & BTN_EVENT_ALL;
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

      if (get_active_callback(BTN_EVENT_DOWN, btn))
        btn->on_down();
    }
    break;

  case BTN_STATE_PRESS:
    if (!is_pressed) {
      btn->state = BTN_STATE_IDLE;

      if (get_active_callback(BTN_EVENT_UP, btn))
        btn->on_up();
      if (get_active_callback(BTN_EVENT_CLICK, btn))
        btn->on_click();

    } else if ((now_ms - btn->state_time_ms) >= LONG_PRESS_TIME_MS) {
      btn->state = BTN_STATE_LONG_PRESS;

      if (get_active_callback(BTN_EVENT_LONG_PRESS_START, btn))
        btn->on_long_press_start();
    }
    break;

  case BTN_STATE_LONG_PRESS:
    if (!is_pressed) {
      btn->state = BTN_STATE_IDLE;

      if (get_active_callback(BTN_EVENT_UP, btn))
        btn->on_up();
      if (get_active_callback(BTN_EVENT_LONG_PRESS_END, btn))
        btn->on_long_press_end();
    }
    break;

  default:
    btn->state = BTN_STATE_IDLE;
    break;
  }
}
