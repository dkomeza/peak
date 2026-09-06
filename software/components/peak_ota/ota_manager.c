#include "peak_ota/ota_manager.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "psa/crypto.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

static const char *TAG = "peak_ota";

typedef struct {
  peak_ota_status_cb_t callback;
  void *user_data;
  peak_ota_status_t status;
} status_notification_t;

static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;
static peak_ota_status_t s_status = {
    .state = PEAK_OTA_STATE_IDLE,
    .last_error = ESP_OK,
};
static peak_ota_status_cb_t s_status_cb;
static void *s_status_user_data;

static const esp_partition_t *s_partition;
static esp_ota_handle_t s_ota_handle;
static bool s_ota_active;
static psa_hash_operation_t s_hash_operation = PSA_HASH_OPERATION_INIT;
static bool s_hash_active;
static uint8_t s_expected_sha256[PEAK_OTA_SHA256_LEN];
static uint32_t s_session_id;
static uint32_t s_last_notified_bytes;

static status_notification_t make_notification_locked(void) {
  return (status_notification_t){
      .callback = s_status_cb,
      .user_data = s_status_user_data,
      .status = s_status,
  };
}

static void publish_notification(const status_notification_t *notification) {
  ESP_LOGI(TAG, "state=%s bytes=%" PRIu32 "/%" PRIu32 " error=%s",
           peak_ota_state_to_string(notification->status.state),
           notification->status.bytes_written,
           notification->status.total_size,
           esp_err_to_name(notification->status.last_error));

  if (notification->callback != NULL) {
    notification->callback(&notification->status, notification->user_data);
  }
}

static void reset_session_locked(void) {
  s_partition = NULL;
  s_ota_handle = 0;
  s_ota_active = false;
  memset(s_expected_sha256, 0, sizeof(s_expected_sha256));

  if (s_hash_active) {
    psa_hash_abort(&s_hash_operation);
  }
  s_hash_operation = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
  s_hash_active = false;
}

static void abort_ota_handle_locked(void) {
  if (!s_ota_active) {
    return;
  }

  esp_err_t abort_err = esp_ota_abort(s_ota_handle);
  if (abort_err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to abort OTA handle: %s",
             esp_err_to_name(abort_err));
  }
  s_ota_active = false;
  s_ota_handle = 0;
}

static status_notification_t fail_session_locked(esp_err_t error) {
  abort_ota_handle_locked();

  if (s_hash_active) {
    psa_status_t hash_status = psa_hash_abort(&s_hash_operation);
    if (hash_status != PSA_SUCCESS) {
      ESP_LOGW(TAG, "Failed to abort SHA-256 operation: %d",
               (int)hash_status);
    }
  }
  s_hash_operation = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
  s_hash_active = false;
  s_partition = NULL;
  s_status.state = PEAK_OTA_STATE_FAILED;
  s_status.last_error = error;
  return make_notification_locked();
}

static bool state_can_begin(peak_ota_state_t state) {
  return state == PEAK_OTA_STATE_IDLE || state == PEAK_OTA_STATE_FAILED ||
         state == PEAK_OTA_STATE_ABORTED;
}

esp_err_t peak_ota_init(peak_ota_status_cb_t status_cb, void *user_data) {
  psa_status_t crypto_status = psa_crypto_init();
  if (crypto_status != PSA_SUCCESS) {
    ESP_LOGE(TAG, "Failed to initialize PSA Crypto: %d", (int)crypto_status);
    return ESP_FAIL;
  }

  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
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

esp_err_t peak_ota_begin(uint32_t image_size,
                         const uint8_t expected_sha256[PEAK_OTA_SHA256_LEN]) {
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (expected_sha256 == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (image_size == 0) {
    return ESP_ERR_INVALID_SIZE;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!state_can_begin(s_status.state)) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  reset_session_locked();
  uint32_t session_id = ++s_session_id;
  s_last_notified_bytes = 0;
  memcpy(s_expected_sha256, expected_sha256, sizeof(s_expected_sha256));
  s_status = (peak_ota_status_t){
      .state = PEAK_OTA_STATE_PREPARING,
      .bytes_written = 0,
      .total_size = image_size,
      .last_error = ESP_OK,
  };
  status_notification_t preparing = make_notification_locked();
  xSemaphoreGive(s_mutex);
  publish_notification(&preparing);

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_status.state != PEAK_OTA_STATE_PREPARING ||
      s_session_id != session_id) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  s_partition = esp_ota_get_next_update_partition(NULL);
  if (s_partition == NULL) {
    status_notification_t failed = fail_session_locked(ESP_ERR_NOT_FOUND);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return ESP_ERR_NOT_FOUND;
  }
  if (image_size > s_partition->size) {
    status_notification_t failed = fail_session_locked(ESP_ERR_INVALID_SIZE);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return ESP_ERR_INVALID_SIZE;
  }

  psa_status_t hash_status =
      psa_hash_setup(&s_hash_operation, PSA_ALG_SHA_256);
  if (hash_status != PSA_SUCCESS) {
    ESP_LOGE(TAG, "Failed to initialize SHA-256 operation: %d",
             (int)hash_status);
    status_notification_t failed = fail_session_locked(ESP_FAIL);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return ESP_FAIL;
  }
  s_hash_active = true;

  esp_err_t err = esp_ota_begin(s_partition, image_size, &s_ota_handle);
  if (err != ESP_OK) {
    status_notification_t failed = fail_session_locked(err);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return err;
  }
  s_ota_active = true;

  s_status.state = PEAK_OTA_STATE_READY;
  status_notification_t ready = make_notification_locked();
  xSemaphoreGive(s_mutex);
  publish_notification(&ready);
  return ESP_OK;
}

esp_err_t peak_ota_write(uint32_t offset, const uint8_t *data, size_t len) {
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (data == NULL || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_ota_active ||
      (s_status.state != PEAK_OTA_STATE_READY &&
       s_status.state != PEAK_OTA_STATE_RECEIVING)) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  if (offset != s_status.bytes_written) {
    status_notification_t failed = fail_session_locked(ESP_ERR_INVALID_ARG);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return ESP_ERR_INVALID_ARG;
  }

  uint32_t remaining = s_status.total_size - s_status.bytes_written;
  if (len > remaining) {
    status_notification_t failed = fail_session_locked(ESP_ERR_INVALID_SIZE);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return ESP_ERR_INVALID_SIZE;
  }

  esp_err_t err = esp_ota_write(s_ota_handle, data, len);
  if (err != ESP_OK) {
    status_notification_t failed = fail_session_locked(err);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return err;
  }

  psa_status_t hash_status = psa_hash_update(&s_hash_operation, data, len);
  if (hash_status != PSA_SUCCESS) {
    ESP_LOGE(TAG, "Failed to update SHA-256 operation: %d", (int)hash_status);
    status_notification_t failed = fail_session_locked(ESP_FAIL);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return ESP_FAIL;
  }

  bool entered_receiving = s_status.state == PEAK_OTA_STATE_READY;
  s_status.state = PEAK_OTA_STATE_RECEIVING;
  s_status.bytes_written += (uint32_t)len;
  s_status.last_error = ESP_OK;
  uint32_t previous_percent =
      (uint32_t)(((uint64_t)s_last_notified_bytes * 100) /
                 s_status.total_size);
  uint32_t current_percent =
      (uint32_t)(((uint64_t)s_status.bytes_written * 100) /
                 s_status.total_size);
  bool should_notify = entered_receiving ||
                       current_percent != previous_percent ||
                       s_status.bytes_written == s_status.total_size;
  status_notification_t receiving = {0};
  if (should_notify) {
    s_last_notified_bytes = s_status.bytes_written;
    receiving = make_notification_locked();
  }
  xSemaphoreGive(s_mutex);
  if (should_notify) {
    publish_notification(&receiving);
  }
  return ESP_OK;
}

esp_err_t peak_ota_finish(void) {
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_ota_active ||
      (s_status.state != PEAK_OTA_STATE_READY &&
       s_status.state != PEAK_OTA_STATE_RECEIVING)) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  if (s_status.bytes_written != s_status.total_size) {
    status_notification_t failed = fail_session_locked(ESP_ERR_INVALID_SIZE);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return ESP_ERR_INVALID_SIZE;
  }

  s_status.state = PEAK_OTA_STATE_VERIFYING;
  uint32_t session_id = s_session_id;
  status_notification_t verifying = make_notification_locked();
  xSemaphoreGive(s_mutex);
  publish_notification(&verifying);

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_ota_active || s_status.state != PEAK_OTA_STATE_VERIFYING ||
      s_session_id != session_id) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  psa_status_t hash_status =
      psa_hash_verify(&s_hash_operation, s_expected_sha256,
                      sizeof(s_expected_sha256));
  if (hash_status != PSA_SUCCESS) {
    esp_err_t hash_err = hash_status == PSA_ERROR_INVALID_SIGNATURE
                             ? ESP_ERR_INVALID_CRC
                             : ESP_FAIL;
    if (hash_err != ESP_ERR_INVALID_CRC) {
      ESP_LOGE(TAG, "Failed to verify SHA-256 operation: %d",
               (int)hash_status);
    }
    status_notification_t failed = fail_session_locked(hash_err);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return hash_err;
  }
  s_hash_operation = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
  s_hash_active = false;

  esp_err_t err = esp_ota_end(s_ota_handle);
  s_ota_active = false;
  s_ota_handle = 0;
  if (err != ESP_OK) {
    status_notification_t failed = fail_session_locked(err);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return err;
  }

  err = esp_ota_set_boot_partition(s_partition);
  if (err != ESP_OK) {
    status_notification_t failed = fail_session_locked(err);
    xSemaphoreGive(s_mutex);
    publish_notification(&failed);
    return err;
  }

  s_partition = NULL;
  memset(s_expected_sha256, 0, sizeof(s_expected_sha256));
  s_status.state = PEAK_OTA_STATE_SUCCESS;
  s_status.last_error = ESP_OK;
  status_notification_t success = make_notification_locked();
  xSemaphoreGive(s_mutex);
  publish_notification(&success);
  return ESP_OK;
}

esp_err_t peak_ota_abort(void) {
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool is_preparing = s_status.state == PEAK_OTA_STATE_PREPARING;
  bool is_active_state =
      s_status.state == PEAK_OTA_STATE_READY ||
      s_status.state == PEAK_OTA_STATE_RECEIVING ||
      s_status.state == PEAK_OTA_STATE_VERIFYING;
  if (!is_preparing && !is_active_state) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t abort_err = ESP_OK;
  if (s_ota_active) {
    abort_err = esp_ota_abort(s_ota_handle);
    if (abort_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to abort OTA handle: %s",
               esp_err_to_name(abort_err));
    }
  }
  s_ota_active = false;
  s_ota_handle = 0;

  if (s_hash_active) {
    psa_status_t hash_status = psa_hash_abort(&s_hash_operation);
    if (hash_status != PSA_SUCCESS && abort_err == ESP_OK) {
      ESP_LOGE(TAG, "Failed to abort SHA-256 operation: %d",
               (int)hash_status);
      abort_err = ESP_FAIL;
    }
  }
  s_hash_operation = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
  s_hash_active = false;
  s_partition = NULL;
  memset(s_expected_sha256, 0, sizeof(s_expected_sha256));
  s_status.state = PEAK_OTA_STATE_ABORTED;
  s_status.last_error = abort_err;
  status_notification_t aborted = make_notification_locked();
  xSemaphoreGive(s_mutex);
  publish_notification(&aborted);
  return abort_err;
}

esp_err_t peak_ota_get_status(peak_ota_status_t *status) {
  if (status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
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
  case PEAK_OTA_STATE_PREPARING:
    return "preparing";
  case PEAK_OTA_STATE_READY:
    return "ready";
  case PEAK_OTA_STATE_RECEIVING:
    return "receiving";
  case PEAK_OTA_STATE_VERIFYING:
    return "verifying";
  case PEAK_OTA_STATE_SUCCESS:
    return "success";
  case PEAK_OTA_STATE_FAILED:
    return "failed";
  case PEAK_OTA_STATE_ABORTED:
    return "aborted";
  default:
    return "unknown";
  }
}
