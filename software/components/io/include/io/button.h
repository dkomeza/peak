#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*callback_t)(void);

typedef enum ButtonEventType {
  BTN_EVENT_DOWN = 1,
  BTN_EVENT_UP = 2,
  BTN_EVENT_CLICK = 4,
  BTN_EVENT_LONG_PRESS_START = 8,
  BTN_EVENT_LONG_PRESS_END = 16,
  BTN_EVENT_ALL = 31, // Combination of all events
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
  uint8_t pause_mask;

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
 * Check the current state of the button.
 */
bool button_is_pressed(volatile button_state_t *btn);

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

/** Pauses the button handling, preventing any callbacks from being triggered
 * until resumed. It doesn't clear current callbacks, and allows adding new ones
 * while paused. */
void button_pause_callback(btn_event_type_t event_type,
                           volatile button_state_t *btn);

/**
 * Resumes the button handling after being paused, allowing callbacks to be
 * executed again when the specified events occur.
 */
void button_resume_callback(btn_event_type_t event_type,
                            volatile button_state_t *btn);

/**
 * This function should be called periodically (e.g., in a main loop or timer)
 * to update the button state and trigger the appropriate callbacks based on the
 * button events. It should be executed at most every 10ms to ensure responsive
 * button handling.
 */
void button_update(volatile button_state_t *btn);

#endif
