#ifndef PEAK_DISPLAY_H
#define PEAK_DISPLAY_H

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
  DISPLAY_BUTTON_EVENT_NONE = 0,
  DISPLAY_BUTTON_EVENT_UP,
  DISPLAY_BUTTON_EVENT_POWER,
  DISPLAY_BUTTON_EVENT_DOWN,
} display_button_event_t;

esp_err_t display_init(void);
void display_show_button_event(display_button_event_t event, bool error);

#endif
