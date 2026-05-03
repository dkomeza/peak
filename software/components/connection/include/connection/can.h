#ifndef CAN_H
#define CAN_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The signature for your UI callbacks
typedef void (*can_bus_receive_cb_t)(uint32_t id, const uint8_t *data,
                                     uint8_t len, void *user_data);

/**
 * Initializes the CAN driver and starts the background dispatcher task.
 */
esp_err_t can_init(void);

/**
 * Registers a callback to fire when a received ID matches the filter.
 * Logic: if ((received_id & mask) == id) -> trigger callback
 *
 * @param id The ID (or base ID) to match.
 * @param mask The mask to apply (e.g., 0xFFFFFFFF for exact match, 0xFF000000
 * for range).
 * @param cb The function to call.
 * @param user_data Optional pointer to pass state (like a UI widget pointer).
 */
esp_err_t can_register_cb(uint32_t id, uint32_t mask, can_bus_receive_cb_t cb,
                          void *user_data);

/**
 * Thread-safe CAN transmit.
 */
esp_err_t can_send(uint32_t id, const uint8_t *data, uint8_t len,
                   uint16_t timeout_ms);

#endif
