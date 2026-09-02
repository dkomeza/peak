#ifndef PEAK_BACKLIGHT_H
#define PEAK_BACKLIGHT_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t backlight_init(void);
/** Fade the backlight on or off. */
esp_err_t backlight_set_enabled(bool enabled);
/** Set perceptual brightness and disable automatic adjustment. */
esp_err_t backlight_set_percent(uint8_t percent);
esp_err_t backlight_set_auto_enabled(bool enabled);

/** Poll the ambient-light sensor and update brightness when auto mode is on. */
esp_err_t backlight_service(void);

#endif
