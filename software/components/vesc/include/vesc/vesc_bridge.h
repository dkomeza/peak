#ifndef VESC_BRIDGE_H
#define VESC_BRIDGE_H

#include "esp_err.h"
#include "vesc/transport_iface.h"
#include <stddef.h>

esp_err_t vesc_bridge_init(void);
esp_err_t vesc_bridge_start(const transport_iface_t *const *transports,
                            size_t transport_count);
esp_err_t vesc_bridge_stop(void);

#endif
