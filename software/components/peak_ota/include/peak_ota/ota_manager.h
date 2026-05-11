#ifndef PEAK_OTA_MANAGER_H
#define PEAK_OTA_MANAGER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PEAK_OTA_URL_MAX_LEN 256
#define PEAK_OTA_STATUS_JSON_MAX_LEN 256

typedef enum {
  PEAK_OTA_STATE_IDLE = 0,
  PEAK_OTA_STATE_READY,
  PEAK_OTA_STATE_STARTING,
  PEAK_OTA_STATE_DOWNLOADING,
  PEAK_OTA_STATE_SUCCESS,
  PEAK_OTA_STATE_FAILED,
} peak_ota_state_t;

typedef struct {
  peak_ota_state_t state;
  char url[PEAK_OTA_URL_MAX_LEN];
  int bytes_read;
  int image_size;
  int percent;
  uint32_t speed_bps;
  esp_err_t last_error;
  char message[64];
} peak_ota_status_t;

typedef void (*peak_ota_status_cb_t)(const peak_ota_status_t *status,
                                     void *user_data);

esp_err_t peak_ota_init(peak_ota_status_cb_t status_cb, void *user_data);
esp_err_t peak_ota_set_url(const char *url);
esp_err_t peak_ota_start(void);
esp_err_t peak_ota_get_status(peak_ota_status_t *status);
const char *peak_ota_state_to_string(peak_ota_state_t state);
int peak_ota_status_to_json(const peak_ota_status_t *status, char *buffer,
                            size_t buffer_len);

#ifdef __cplusplus
}
#endif

#endif
