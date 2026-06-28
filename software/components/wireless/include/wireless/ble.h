#ifndef BLE_H
#define BLE_H

#include "esp_err.h"
#include "host/ble_hs.h"
#include <stdint.h>

/**
 * @brief Initializes NVS, the BLE stack, registers services, and starts
 * advertising.
 *
 * @param device_name The name broadcasted to scanning devices.
 * @param services Array of GATT services to register.
 * @param adv_uuid Optional primary service UUID to include in the
 * advertisement.
 */
esp_err_t ble_manager_start(const char *device_name,
                            const struct ble_gatt_svc_def *services,
                            const ble_uuid128_t *adv_uuid);

/**
 * @brief Registers additional GATT services before the BLE manager starts.
 */
esp_err_t ble_manager_register_services(
    const struct ble_gatt_svc_def *services);

/**
 * @brief Gets the active BLE connection handle.
 * @return The handle, or BLE_HS_CONN_HANDLE_NONE if no device is connected.
 */
uint16_t ble_manager_get_conn_handle(void);

/**
 * @brief Safely terminates connections and stops the BLE radio to save power.
 */
esp_err_t ble_manager_stop(void);

#endif
