#ifndef UDP_BRIDGE_H
#define UDP_BRIDGE_H

#include "esp_err.h"
#include "transport_iface.h"
#include <stddef.h>
#include <stdint.h>

typedef void (*udp_receive_cb_t)(const uint8_t *data, size_t len,
                                 void *user_data);

/**
 * @brief Starts the UDP listening task.
 */
esp_err_t udp_bridge_start(uint16_t port, udp_receive_cb_t callback,
                           void *user_data);

/**
 * @brief Sends data back to the last known peer. Thread-safe.
 */
esp_err_t udp_bridge_send(const uint8_t *data, size_t len);

const transport_iface_t *get_udp_transport_iface(void);

#endif
