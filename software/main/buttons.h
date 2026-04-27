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
bool buttons_is_pressed(button_t btn);
void buttons_on(button_t btn, btn_event_type_t event_type, callback_t callback);
void buttons_clear(button_t btn, btn_event_type_t event_type);
void buttons_pause(button_t btn, btn_event_type_t event_type);
void buttons_resume(button_t btn, btn_event_type_t event_type);

#endif
