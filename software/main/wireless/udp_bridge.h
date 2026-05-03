#ifndef UDP_BRIDGE_H
#define UDP_BRIDGE_H

#include "transport_iface.h"
#include <stddef.h>
#include <stdint.h>

typedef void (*udp_receive_cb_t)(const uint8_t *data, size_t len,
                                 void *user_data);

extern const transport_iface_t transport_udp;

#endif
