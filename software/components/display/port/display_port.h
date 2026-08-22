#ifndef PEAK_DISPLAY_PORT_H
#define PEAK_DISPLAY_PORT_H

#include "esp_err.h"
#include <stdint.h>

/*
 * These functions must be called by the UI gateway task. The display port
 * owns panel, draw-buffer, flush, and LVGL tick plumbing but creates no UI.
 */
esp_err_t display_port_init(void);
uint32_t display_port_timer_handler(void);

uint32_t display_cpu_idle_percent(void);

#endif
