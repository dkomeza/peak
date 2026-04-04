#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>

typedef void (*callback_t)(void);

typedef enum ButtonEventType {
  BTN_EVENT_DOWN,
  BTN_EVENT_UP,
  BTN_EVENT_CLICK,
  BTN_EVENT_LONG_PRESS_START,
  BTN_EVENT_LONG_PRESS_END,
  BTN_EVENT_ALL,
} btn_event_type_t;

typedef enum {
  BTN_STATE_IDLE,
  BTN_STATE_DEBOUNCE,
  BTN_STATE_PRESS,
  BTN_STATE_LONG_PRESS,
} btn_state_t;

typedef struct {
  int pin;
  btn_state_t state;
  uint32_t state_time_ms;

  // Callbacks
  callback_t on_down;
  callback_t on_up;
  callback_t on_click;
  callback_t on_long_press_start;
  callback_t on_long_press_end;
} button_state_t;

/**
 * Initializes a button on the specified GPIO pin.
 */
button_state_t *button_init(int pin);

/**
 * Attaches a callback function to a specific button event type.
 */
void button_attach_callback(btn_event_type_t event_type, callback_t callback,
                            volatile button_state_t *btn);

/**
 * Removes the callback function for a specific button event type.
 * If the event type is BTN_EVENT_ALL, all callbacks for the button will be
 * cleared.
 */
void button_detach_callback(btn_event_type_t event_type,
                            volatile button_state_t *btn);

/**
 * This function should be called periodically (e.g., in a main loop or timer)
 * to update the button state and trigger the appropriate callbacks based on the
 * button events. It should be executed at most every 10ms to ensure responsive
 * button handling.
 */
void button_update(volatile button_state_t *btn);

#endif
