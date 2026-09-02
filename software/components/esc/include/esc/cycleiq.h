#ifndef ESC_CYCLEIQ_H
#define ESC_CYCLEIQ_H

#include "esc/esc.h"

esp_err_t esc_cycleiq_init(void);
esp_err_t esc_cycleiq_gear_up(void);
esp_err_t esc_cycleiq_gear_down(void);
esp_err_t esc_cycleiq_set_support_mode(esc_support_mode_t mode);
esp_err_t esc_cycleiq_set_ride_mode(esc_ride_mode_t mode);
esp_err_t esc_cycleiq_set_walk(bool enabled);

#endif
