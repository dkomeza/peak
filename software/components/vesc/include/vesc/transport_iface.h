#ifndef TRANSPORT_IFACE_H
#define TRANSPORT_IFACE_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// The callback signature whenever data arrives from the outside world
typedef void (*transport_rx_cb_t)(const uint8_t *data, size_t len,
                                  void *user_data);

// The v-table struct that defines a transport method
typedef struct {
  const char *name; // e.g., "UDP Bridge" or "BLE Bridge"
  esp_err_t (*start)(transport_rx_cb_t rx_cb, void *user_data);
  esp_err_t (*send)(const uint8_t *data, size_t len);
  esp_err_t (*stop)(void); // Useful for switching dynamically
} transport_iface_t;

#endif
