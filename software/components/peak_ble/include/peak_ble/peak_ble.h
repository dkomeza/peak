#ifndef PEAK_BLE_H
#define PEAK_BLE_H

#include "esp_err.h"
#include "vesc/transport_iface.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  /** Allow direct OTA BEGIN commands for this boot. Keep false by default. */
  bool ota_enabled;
} peak_ble_config_t;

/** Start the ESP-Hosted Bluetooth controller, NimBLE host, and PEAK services.
 */
esp_err_t peak_ble_start(const peak_ble_config_t *config);

/** Stop PEAK BLE and abort any in-progress BLE OTA session. */
esp_err_t peak_ble_stop(void);

/** Nordic UART Service transport for use with vesc_bridge_start(). */
extern const transport_iface_t transport_peak_ble;

#ifdef __cplusplus
}
#endif

#endif
