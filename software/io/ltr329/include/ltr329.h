#ifndef LTR329_H
#define LTR329_H

#include "driver/i2c_types.h"

void ltr329_sensor_init(i2c_master_bus_handle_t *bus_handle);
float ltr329_read_lux();

#endif
