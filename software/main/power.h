#ifndef PEAK_POWER_H
#define PEAK_POWER_H

#include "esp_err.h"

/** Configure Power-button wake and enter deep sleep. Does not return on success. */
esp_err_t power_enter_deep_sleep(void);

#endif
