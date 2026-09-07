#ifndef PEAK_OTA_MANAGER_H
#define PEAK_OTA_MANAGER_H

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PEAK_OTA_SHA256_LEN 32

typedef enum {
  PEAK_OTA_STATE_IDLE = 0,
  PEAK_OTA_STATE_PREPARING = 1,
  PEAK_OTA_STATE_READY = 2,
  PEAK_OTA_STATE_RECEIVING = 3,
  PEAK_OTA_STATE_VERIFYING = 4,
  PEAK_OTA_STATE_SUCCESS = 5,
  PEAK_OTA_STATE_FAILED = 6,
  PEAK_OTA_STATE_ABORTED = 7,
} peak_ota_state_t;

typedef struct {
  peak_ota_state_t state;
  uint32_t bytes_written;
  uint32_t total_size;
  esp_err_t last_error;
} peak_ota_status_t;

typedef void (*peak_ota_status_cb_t)(const peak_ota_status_t *status,
                                     void *user_data);

/**
 * @brief Initialize the OTA session engine.
 *
 * This function must be called before any other peak_ota function. Calling it
 * again updates the callback without disturbing an active session.
 */
esp_err_t peak_ota_init(peak_ota_status_cb_t status_cb, void *user_data);

/**
 * @brief Start a new sequential OTA session.
 *
 * A new session can be started from IDLE, FAILED, or ABORTED. SUCCESS requires
 * a reboot before another session because the new boot partition is selected.
 */
esp_err_t peak_ota_begin(uint32_t image_size,
                         const uint8_t expected_sha256[PEAK_OTA_SHA256_LEN]);

/**
 * @brief Write the next firmware block.
 *
 * Offset must equal status.bytes_written. Invalid offsets, writes beyond the
 * declared image size, and flash or hash failures terminate the session.
 */
esp_err_t peak_ota_write(uint32_t offset, const uint8_t *data, size_t len);

/**
 * @brief Validate the completed image and select it for the next boot.
 */
esp_err_t peak_ota_finish(void);

/**
 * @brief Abort an active session without changing the boot partition.
 */
esp_err_t peak_ota_abort(void);

esp_err_t peak_ota_get_status(peak_ota_status_t *status);
const char *peak_ota_state_to_string(peak_ota_state_t state);

#ifdef __cplusplus
}
#endif

#endif
