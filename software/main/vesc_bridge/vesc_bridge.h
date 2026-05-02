#ifndef VESC_BRIDGE_H
#define VESC_BRIDGE_H

#include "esp_err.h"
#include "transport_iface.h"

esp_err_t vesc_bridge_init(void);
esp_err_t vesc_bridge_start(const transport_iface_t *transport);
esp_err_t vesc_bridge_stop(void);

#endif
