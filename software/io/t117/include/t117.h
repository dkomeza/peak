#ifndef T117_H
#define T117_H

#include "driver/i2c_types.h"

void t117_sensor_init(i2c_master_bus_handle_t *bus_handle);
float t117_read_temperature();

#endif
