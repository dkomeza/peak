#include "peak_ota/ota_manager.h"

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "peak_ota";

#define PEAK_OTA_TASK_STACK_SIZE 8192
#define PEAK_OTA_TASK_PRIORITY 5
#define PEAK_OTA_HTTP_TIMEOUT_MS 10000
#define PEAK_OTA_RESTART_DELAY_MS 1500

static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task_handle;
static peak_ota_status_t s_status = {
    .state = PEAK_OTA_STATE_IDLE,
    .percent = -1,
};
static peak_ota_status_cb_t s_status_cb;
static void *s_status_user_data;

static bool url_is_supported(const char *url) {
  return url != NULL &&
         (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

static void copy_text(char *dst, size_t dst_len, const char *src) {
  if (dst_len == 0) {
    return;
  }

  snprintf(dst, dst_len, "%s", src != NULL ? src : "");
}

static void publish_status(void) {
  peak_ota_status_t snapshot;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  snapshot = s_status;
  xSemaphoreGive(s_mutex);

  ESP_LOGI(TAG,
           "state=%s percent=%d bytes=%d total=%d speed=%" PRIu32
           " error=%s message=%s",
           peak_ota_state_to_string(snapshot.state), snapshot.percent,
           snapshot.bytes_read, snapshot.image_size, snapshot.speed_bps,
           esp_err_to_name(snapshot.last_error), snapshot.message);

  if (s_status_cb != NULL) {
    s_status_cb(&snapshot, s_status_user_data);
  }
}

static void update_status(peak_ota_state_t state, int bytes_read,
                          int image_size, uint32_t speed_bps,
                          esp_err_t last_error, const char *message) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);

  s_status.state = state;
  s_status.bytes_read = bytes_read;
  s_status.image_size = image_size;
  s_status.speed_bps = speed_bps;
  s_status.last_error = last_error;
  s_status.percent =
      (image_size > 0 && bytes_read >= 0) ? (bytes_read * 100) / image_size : -1;
  copy_text(s_status.message, sizeof(s_status.message), message);

  xSemaphoreGive(s_mutex);
  publish_status();
}

static void ota_task(void *arg) {
  (void)arg;

  char url[PEAK_OTA_URL_MAX_LEN];
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  copy_text(url, sizeof(url), s_status.url);
  xSemaphoreGive(s_mutex);

  update_status(PEAK_OTA_STATE_STARTING, 0, -1, 0, ESP_OK, "starting");

  esp_http_client_config_t http_config = {
      .url = url,
      .timeout_ms = PEAK_OTA_HTTP_TIMEOUT_MS,
      .keep_alive_enable = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
  };
  esp_https_ota_config_t ota_config = {
      .http_config = &http_config,
  };

  esp_https_ota_handle_t ota_handle = NULL;
  esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
    update_status(PEAK_OTA_STATE_FAILED, 0, -1, 0, err, "begin failed");
    goto done;
  }

  int image_size = esp_https_ota_get_image_size(ota_handle);
  int64_t start_us = esp_timer_get_time();
  int64_t last_publish_us = 0;
  int last_percent = -2;

  update_status(PEAK_OTA_STATE_DOWNLOADING, 0, image_size, 0, ESP_OK,
                "downloading");

  while (true) {
    err = esp_https_ota_perform(ota_handle);

    int bytes_read = esp_https_ota_get_image_len_read(ota_handle);
    image_size = esp_https_ota_get_image_size(ota_handle);
    int percent =
        (image_size > 0 && bytes_read >= 0) ? (bytes_read * 100) / image_size
                                            : -1;
    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_us = now_us - start_us;
    uint32_t speed_bps =
        (elapsed_us > 0 && bytes_read > 0)
            ? (uint32_t)(((int64_t)bytes_read * 1000000) / elapsed_us)
            : 0;

    if (percent != last_percent || now_us - last_publish_us > 1000000) {
      update_status(PEAK_OTA_STATE_DOWNLOADING, bytes_read, image_size,
                    speed_bps, ESP_OK, "downloading");
      last_publish_us = now_us;
      last_percent = percent;
    }

    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      break;
    }
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
    int bytes_read = esp_https_ota_get_image_len_read(ota_handle);
    esp_https_ota_abort(ota_handle);
    update_status(PEAK_OTA_STATE_FAILED, bytes_read, image_size, 0, err,
                  "download failed");
    goto done;
  }

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    int bytes_read = esp_https_ota_get_image_len_read(ota_handle);
    esp_https_ota_abort(ota_handle);
    update_status(PEAK_OTA_STATE_FAILED, bytes_read, image_size, 0,
                  ESP_ERR_INVALID_SIZE, "incomplete image");
    goto done;
  }

  int bytes_read = esp_https_ota_get_image_len_read(ota_handle);
  err = esp_https_ota_finish(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
    update_status(PEAK_OTA_STATE_FAILED, bytes_read, image_size, 0, err,
                  "validation failed");
    goto done;
  }

  update_status(PEAK_OTA_STATE_SUCCESS, image_size, image_size, 0, ESP_OK,
                "rebooting");
  vTaskDelay(pdMS_TO_TICKS(PEAK_OTA_RESTART_DELAY_MS));
  esp_restart();

done:
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_task_handle = NULL;
  xSemaphoreGive(s_mutex);
  vTaskDelete(NULL);
}

esp_err_t peak_ota_init(peak_ota_status_cb_t status_cb, void *user_data) {
  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_status_cb = status_cb;
  s_status_user_data = user_data;
  xSemaphoreGive(s_mutex);

  return ESP_OK;
}

esp_err_t peak_ota_set_url(const char *url) {
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!url_is_supported(url)) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t url_len = strlen(url);
  if (url_len == 0 || url_len >= PEAK_OTA_URL_MAX_LEN) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_task_handle != NULL) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  copy_text(s_status.url, sizeof(s_status.url), url);
  s_status.state = PEAK_OTA_STATE_READY;
  s_status.bytes_read = 0;
  s_status.image_size = -1;
  s_status.percent = -1;
  s_status.speed_bps = 0;
  s_status.last_error = ESP_OK;
  copy_text(s_status.message, sizeof(s_status.message), "url set");
  xSemaphoreGive(s_mutex);

  publish_status();
  return ESP_OK;
}

esp_err_t peak_ota_start(void) {
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_task_handle != NULL) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  if (!url_is_supported(s_status.url)) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  xSemaphoreGive(s_mutex);

  if (xTaskCreate(ota_task, "peak_ota", PEAK_OTA_TASK_STACK_SIZE, NULL,
                  PEAK_OTA_TASK_PRIORITY, &s_task_handle) != pdPASS) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_task_handle = NULL;
    xSemaphoreGive(s_mutex);
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

esp_err_t peak_ota_get_status(peak_ota_status_t *status) {
  if (s_mutex == NULL || status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *status = s_status;
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

const char *peak_ota_state_to_string(peak_ota_state_t state) {
  switch (state) {
  case PEAK_OTA_STATE_IDLE:
    return "idle";
  case PEAK_OTA_STATE_READY:
    return "ready";
  case PEAK_OTA_STATE_STARTING:
    return "starting";
  case PEAK_OTA_STATE_DOWNLOADING:
    return "downloading";
  case PEAK_OTA_STATE_SUCCESS:
    return "success";
  case PEAK_OTA_STATE_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

int peak_ota_status_to_json(const peak_ota_status_t *status, char *buffer,
                            size_t buffer_len) {
  if (status == NULL || buffer == NULL || buffer_len == 0) {
    return -1;
  }

  int written =
      snprintf(buffer, buffer_len,
               "{\"state\":\"%s\",\"percent\":%d,\"bytes\":%d,"
               "\"total\":%d,\"speed_bps\":%" PRIu32
               ",\"error\":\"%s\",\"message\":\"%s\"}",
               peak_ota_state_to_string(status->state), status->percent,
               status->bytes_read, status->image_size, status->speed_bps,
               esp_err_to_name(status->last_error), status->message);

  if (written < 0) {
    return -1;
  }

  return (written >= (int)buffer_len) ? (int)buffer_len - 1 : written;
}
