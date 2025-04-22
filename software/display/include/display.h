#pragma once

#include "esp_err.h"

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

esp_err_t initialize_lcd();

/**
 * Set the brightness of the display.
 * 0 = off, 255 = full brightness.
 */
void set_brightness(uint8_t brightness);
void set_smooth_brightness(uint8_t brightness);