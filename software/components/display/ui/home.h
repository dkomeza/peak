#ifndef PEAK_DISPLAY_HOME_H
#define PEAK_DISPLAY_HOME_H

#include "display/events.h"
#include "model.h"

void display_home_create(void);
void display_home_apply_event(const display_event_t *event);
void display_home_update(const display_ui_model_t *model);

#endif
