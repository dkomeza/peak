#ifndef PEAK_ST7701_COMMANDS_H
#define PEAK_ST7701_COMMANDS_H

#include "esp_lcd_st7701.h"
#include <stddef.h>

const st7701_lcd_init_cmd_t *st7701_init_commands(size_t *count);

#endif
