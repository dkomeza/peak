#ifndef BUTTONS_H
#define BUTTONS_H

#include "button.h"

typedef enum Button {
  BTN_UP,
  BTN_POWER,
  BTN_DOWN,
} button_t;

void buttons_init(void);
void buttons_deinit(void);
void button_on(button_t btn, btn_event_type_t event_type, callback_t callback);
void button_clear(button_t btn, btn_event_type_t event_type);

#endif
