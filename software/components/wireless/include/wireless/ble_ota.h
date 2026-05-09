#ifndef BLE_OTA_H
#define BLE_OTA_H

#include "esp_err.h"

/**
 * @brief Starts the BLE OTA control service.
 *
 * Commands are written as UTF-8 text:
 * - "URL http://host/path/firmware.bin" or "SET_URL http://..."
 * - "START"
 * - "STATUS"
 */
esp_err_t ble_ota_start(void);

#endif
