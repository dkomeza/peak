#include "peak_ble/peak_ble.h"
#include "peak_ble_internal.h"

#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "peak_ota/ota_manager.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "peak_ble";

#define PEAK_BLE_WORK_QUEUE_DEPTH 12
#define PEAK_BLE_TX_QUEUE_DEPTH 12
#define PEAK_BLE_VESC_TX_MAX 520
#define PEAK_BLE_WORKER_STACK_SIZE 4096
#define PEAK_BLE_TX_STACK_SIZE 3072
#define PEAK_BLE_TASK_PRIORITY 5
#define PEAK_BLE_STOP_TIMEOUT_MS 2000
#define PEAK_BLE_NOTIFY_RETRIES 4
#define PEAK_BLE_NOTIFY_WAIT_MS 100
#define PEAK_BLE_SUCCESS_REBOOT_DELAY_MS 1500

#define PEAK_BLE_TASK_WORKER_STOPPED BIT0
#define PEAK_BLE_TASK_TX_STOPPED BIT1
#define PEAK_BLE_TASKS_STOPPED                                                 \
  (PEAK_BLE_TASK_WORKER_STOPPED | PEAK_BLE_TASK_TX_STOPPED)

#define PEAK_BLE_STATUS_FLAG_OTA_SUBSCRIBED BIT0
#define PEAK_BLE_STATUS_FLAG_NUS_SUBSCRIBED BIT1

typedef enum {
  PEAK_BLE_TX_NUS = 0,
  PEAK_BLE_TX_OTA_STATUS,
  PEAK_BLE_TX_STOP,
} peak_ble_tx_type_t;

typedef struct {
  peak_ble_tx_type_t type;
  uint16_t conn_handle;
  uint32_t conn_generation;
  uint16_t len;
  uint8_t bytes[PEAK_BLE_VESC_TX_MAX];
} peak_ble_tx_item_t;

typedef struct {
  bool running;
  bool ota_enabled;
  bool ota_active;
  bool nus_subscribed;
  bool ota_status_subscribed;
  uint16_t conn_handle;
  uint16_t mtu;
  uint32_t conn_generation;
  transport_rx_cb_t vesc_rx_cb;
  void *vesc_rx_user_data;
} peak_ble_state_t;

static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static peak_ble_state_t s_state = {
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
    .mtu = BLE_ATT_MTU_DFLT,
};
static uint8_t s_wire_status[PEAK_BLE_STATUS_LEN] = {
    PEAK_BLE_PROTOCOL_VERSION,
    PEAK_OTA_STATE_IDLE,
};

static StaticQueue_t s_work_queue_control;
static uint8_t s_work_queue_storage[PEAK_BLE_WORK_QUEUE_DEPTH *
                                    sizeof(peak_ble_work_item_t)];
static QueueHandle_t s_work_queue;

static StaticQueue_t s_tx_queue_control;
static uint8_t
    s_tx_queue_storage[PEAK_BLE_TX_QUEUE_DEPTH * sizeof(peak_ble_tx_item_t)];
static QueueHandle_t s_tx_queue;

static StaticEventGroup_t s_task_events_storage;
static EventGroupHandle_t s_task_events;
static StaticTask_t s_worker_task_control;
static StackType_t s_worker_task_stack[PEAK_BLE_WORKER_STACK_SIZE];
static TaskHandle_t s_worker_task;
static StaticTask_t s_tx_task_control;
static StackType_t s_tx_task_stack[PEAK_BLE_TX_STACK_SIZE];
static TaskHandle_t s_tx_task;

static bool s_hosted_bt_enabled;
static bool s_nimble_initialized;
static uint8_t s_own_addr_type;
static char s_device_name[10];

static void peak_ble_advertise(void);

static void write_le32(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

static bool connection_matches(uint16_t conn_handle, uint32_t conn_generation) {
  bool matches;
  taskENTER_CRITICAL(&s_state_lock);
  matches = s_state.running && s_state.conn_handle == conn_handle &&
            s_state.conn_generation == conn_generation;
  taskEXIT_CRITICAL(&s_state_lock);
  return matches;
}

static uint8_t status_flags_locked(void) {
  uint8_t flags = 0;
  if (s_state.ota_status_subscribed) {
    flags |= PEAK_BLE_STATUS_FLAG_OTA_SUBSCRIBED;
  }
  if (s_state.nus_subscribed) {
    flags |= PEAK_BLE_STATUS_FLAG_NUS_SUBSCRIBED;
  }
  return flags;
}

void peak_ble_copy_wire_status(uint8_t status[PEAK_BLE_STATUS_LEN]) {
  taskENTER_CRITICAL(&s_state_lock);
  memcpy(status, s_wire_status, PEAK_BLE_STATUS_LEN);
  status[3] = status_flags_locked();
  taskEXIT_CRITICAL(&s_state_lock);
}

bool peak_ble_enqueue_work(uint16_t conn_handle, peak_ble_work_item_t *item) {
  if (item == NULL || s_work_queue == NULL) {
    return false;
  }

  bool valid;
  taskENTER_CRITICAL(&s_state_lock);
  valid = s_state.running && s_state.conn_handle == conn_handle &&
          !(item->type == PEAK_BLE_WORK_VESC_RX && s_state.ota_active);
  if (valid) {
    item->conn_handle = conn_handle;
    item->conn_generation = s_state.conn_generation;
  }
  taskEXIT_CRITICAL(&s_state_lock);

  return valid && xQueueSend(s_work_queue, item, 0) == pdTRUE;
}

static void queue_disconnect_abort(void) {
  bool ota_active;
  taskENTER_CRITICAL(&s_state_lock);
  ota_active = s_state.ota_active;
  taskEXIT_CRITICAL(&s_state_lock);

  if (!ota_active || s_work_queue == NULL) {
    return;
  }

  peak_ble_work_item_t abort_item = {
      .type = PEAK_BLE_WORK_OTA_ABORT,
      .conn_handle = BLE_HS_CONN_HANDLE_NONE,
  };
  if (xQueueSendToFront(s_work_queue, &abort_item, 0) != pdTRUE) {
    peak_ble_work_item_t discarded;
    (void)xQueueReceive(s_work_queue, &discarded, 0);
    (void)xQueueSendToFront(s_work_queue, &abort_item, 0);
  }
}

static bool queue_tx_item(const peak_ble_tx_item_t *item, bool urgent) {
  if (s_tx_queue == NULL) {
    return false;
  }

  if (urgent) {
    /* A terminal OTA state supersedes queued progress and VESC traffic. */
    xQueueReset(s_tx_queue);
    return xQueueSendToFront(s_tx_queue, item, 0) == pdTRUE;
  }
  return xQueueSend(s_tx_queue, item, 0) == pdTRUE;
}

static void queue_current_status(void) {
  peak_ble_tx_item_t item = {
      .type = PEAK_BLE_TX_OTA_STATUS,
      .len = PEAK_BLE_STATUS_LEN,
  };

  taskENTER_CRITICAL(&s_state_lock);
  if (!s_state.running || s_state.conn_handle == BLE_HS_CONN_HANDLE_NONE ||
      !s_state.ota_status_subscribed) {
    taskEXIT_CRITICAL(&s_state_lock);
    return;
  }
  item.conn_handle = s_state.conn_handle;
  item.conn_generation = s_state.conn_generation;
  memcpy(item.bytes, s_wire_status, PEAK_BLE_STATUS_LEN);
  item.bytes[3] = status_flags_locked();
  taskEXIT_CRITICAL(&s_state_lock);

  bool terminal = item.bytes[1] == PEAK_OTA_STATE_SUCCESS ||
                  item.bytes[1] == PEAK_OTA_STATE_FAILED ||
                  item.bytes[1] == PEAK_OTA_STATE_ABORTED;
  if (!queue_tx_item(&item, terminal)) {
    ESP_LOGW(TAG, "Dropping OTA status notification: TX queue full");
  }
}

static peak_ble_ota_error_t wire_error_from_esp(esp_err_t error) {
  switch (error) {
  case ESP_OK:
    return PEAK_BLE_OTA_ERROR_NONE;
  case ESP_ERR_NOT_ALLOWED:
    return PEAK_BLE_OTA_ERROR_NOT_AUTHORIZED;
  case ESP_ERR_INVALID_STATE:
    return PEAK_BLE_OTA_ERROR_INVALID_STATE;
  case ESP_ERR_INVALID_ARG:
    return PEAK_BLE_OTA_ERROR_INVALID_ARGUMENT;
  case ESP_ERR_INVALID_SIZE:
    return PEAK_BLE_OTA_ERROR_INVALID_SIZE;
  case ESP_ERR_INVALID_CRC:
    return PEAK_BLE_OTA_ERROR_DIGEST_MISMATCH;
  case ESP_ERR_NOT_FOUND:
    return PEAK_BLE_OTA_ERROR_NO_UPDATE_PARTITION;
  case ESP_ERR_NO_MEM:
    return PEAK_BLE_OTA_ERROR_RESOURCE_EXHAUSTED;
  case ESP_ERR_TIMEOUT:
    return PEAK_BLE_OTA_ERROR_TIMEOUT;
  default:
    return PEAK_BLE_OTA_ERROR_INTERNAL;
  }
}

static void set_wire_status(uint8_t last_command,
                            const peak_ota_status_t *status,
                            esp_err_t operation_error, bool notify) {
  esp_err_t source_error = status->last_error != ESP_OK ? status->last_error
                                                        : operation_error;
  uint32_t error = wire_error_from_esp(source_error);

  taskENTER_CRITICAL(&s_state_lock);
  s_wire_status[0] = PEAK_BLE_PROTOCOL_VERSION;
  s_wire_status[1] = (uint8_t)status->state;
  s_wire_status[2] = last_command;
  s_wire_status[3] = status_flags_locked();
  write_le32(&s_wire_status[4], status->bytes_written);
  write_le32(&s_wire_status[8], status->total_size);
  write_le32(&s_wire_status[12], error);
  taskEXIT_CRITICAL(&s_state_lock);

  if (notify) {
    queue_current_status();
  }
}

static void set_unauthorized_status(uint32_t image_size) {
  peak_ota_status_t status = {
      .state = PEAK_OTA_STATE_FAILED,
      .bytes_written = 0,
      .total_size = image_size,
      .last_error = ESP_ERR_NOT_ALLOWED,
  };
  set_wire_status(0x01, &status, ESP_ERR_NOT_ALLOWED, true);
}

static bool ota_state_is_active(peak_ota_state_t state) {
  return state == PEAK_OTA_STATE_PREPARING || state == PEAK_OTA_STATE_READY ||
         state == PEAK_OTA_STATE_RECEIVING || state == PEAK_OTA_STATE_VERIFYING;
}

static uint32_t read_le32(const uint8_t *bytes) {
  return ((uint32_t)bytes[0]) | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static bool data_status_should_notify(const peak_ota_status_t *status,
                                      esp_err_t operation_error) {
  uint8_t previous_state;
  uint32_t previous_written;
  uint32_t previous_total;
  uint32_t previous_error;

  taskENTER_CRITICAL(&s_state_lock);
  previous_state = s_wire_status[1];
  previous_written = read_le32(&s_wire_status[4]);
  previous_total = read_le32(&s_wire_status[8]);
  previous_error = read_le32(&s_wire_status[12]);
  taskEXIT_CRITICAL(&s_state_lock);

  esp_err_t source_error = status->last_error != ESP_OK ? status->last_error
                                                        : operation_error;
  uint32_t error = wire_error_from_esp(source_error);
  if (operation_error != ESP_OK || previous_state != (uint8_t)status->state ||
      previous_error != error || previous_total != status->total_size ||
      status->bytes_written == status->total_size) {
    return true;
  }
  if (status->total_size == 0) {
    return false;
  }

  uint32_t previous_percent =
      (uint32_t)(((uint64_t)previous_written * 100) / status->total_size);
  uint32_t current_percent =
      (uint32_t)(((uint64_t)status->bytes_written * 100) / status->total_size);
  return previous_percent != current_percent;
}

static void publish_core_status(uint8_t last_command, esp_err_t operation_error,
                                bool force_notify) {
  peak_ota_status_t status;
  esp_err_t err = peak_ota_get_status(&status);
  if (err != ESP_OK) {
    status = (peak_ota_status_t){
        .state = PEAK_OTA_STATE_FAILED,
        .last_error = err,
    };
    operation_error = err;
  }
  bool notify =
      force_notify || data_status_should_notify(&status, operation_error);
  taskENTER_CRITICAL(&s_state_lock);
  s_state.ota_active = ota_state_is_active(status.state);
  taskEXIT_CRITICAL(&s_state_lock);
  set_wire_status(last_command, &status, operation_error, notify);
}

static bool work_item_is_current(const peak_ble_work_item_t *item) {
  if (item->type == PEAK_BLE_WORK_STOP) {
    return true;
  }
  if (item->type == PEAK_BLE_WORK_OTA_ABORT &&
      item->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return true;
  }
  return connection_matches(item->conn_handle, item->conn_generation);
}

static void process_vesc_rx(const peak_ble_work_item_t *item) {
  transport_rx_cb_t callback;
  void *user_data;

  taskENTER_CRITICAL(&s_state_lock);
  callback = s_state.vesc_rx_cb;
  user_data = s_state.vesc_rx_user_data;
  taskEXIT_CRITICAL(&s_state_lock);

  if (callback != NULL) {
    callback(item->payload.vesc.bytes, item->payload.vesc.len, user_data);
  }
}

static void process_ota_work(const peak_ble_work_item_t *item) {
  esp_err_t err = ESP_OK;
  uint8_t command = 0;

  switch (item->type) {
  case PEAK_BLE_WORK_OTA_BEGIN: {
    command = 0x01;
    bool ota_enabled;
    taskENTER_CRITICAL(&s_state_lock);
    ota_enabled = s_state.ota_enabled;
    taskEXIT_CRITICAL(&s_state_lock);
    if (!ota_enabled) {
      ESP_LOGW(TAG, "Rejecting BLE OTA outside the maintenance window");
      set_unauthorized_status(item->payload.ota_begin.image_size);
      return;
    }

    err = peak_ota_begin(item->payload.ota_begin.image_size,
                         item->payload.ota_begin.sha256);
    taskENTER_CRITICAL(&s_state_lock);
    s_state.ota_active = err == ESP_OK;
    taskEXIT_CRITICAL(&s_state_lock);

    if (err == ESP_OK &&
        !connection_matches(item->conn_handle, item->conn_generation)) {
      err = peak_ota_abort();
      taskENTER_CRITICAL(&s_state_lock);
      s_state.ota_active = false;
      taskEXIT_CRITICAL(&s_state_lock);
      publish_core_status(0x03, err, true);
      return;
    }
    break;
  }

  case PEAK_BLE_WORK_OTA_DATA:
    command = 0;
    err = peak_ota_write(item->payload.ota_data.offset,
                         item->payload.ota_data.bytes,
                         item->payload.ota_data.len);
    break;

  case PEAK_BLE_WORK_OTA_FINISH:
    command = 0x02;
    err = peak_ota_finish();
    taskENTER_CRITICAL(&s_state_lock);
    s_state.ota_active = false;
    taskEXIT_CRITICAL(&s_state_lock);
    break;

  case PEAK_BLE_WORK_OTA_ABORT:
    command = 0x03;
    err = peak_ota_abort();
    taskENTER_CRITICAL(&s_state_lock);
    s_state.ota_active = false;
    taskEXIT_CRITICAL(&s_state_lock);
    break;

  case PEAK_BLE_WORK_OTA_QUERY:
    command = 0x04;
    break;

  default:
    return;
  }

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "OTA command 0x%02x failed: %s", command,
             esp_err_to_name(err));
  }
  publish_core_status(command, err, item->type != PEAK_BLE_WORK_OTA_DATA);

  peak_ota_status_t status;
  if (item->type == PEAK_BLE_WORK_OTA_FINISH && err == ESP_OK &&
      peak_ota_get_status(&status) == ESP_OK &&
      status.state == PEAK_OTA_STATE_SUCCESS) {
    ESP_LOGI(TAG, "BLE OTA succeeded; rebooting in %d ms",
             PEAK_BLE_SUCCESS_REBOOT_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(PEAK_BLE_SUCCESS_REBOOT_DELAY_MS));
    esp_restart();
  }
}

static void worker_task(void *arg) {
  (void)arg;
  peak_ble_work_item_t item;

  while (xQueueReceive(s_work_queue, &item, portMAX_DELAY) == pdTRUE) {
    if (item.type == PEAK_BLE_WORK_STOP) {
      bool ota_active;
      taskENTER_CRITICAL(&s_state_lock);
      ota_active = s_state.ota_active;
      s_state.ota_active = false;
      taskEXIT_CRITICAL(&s_state_lock);
      if (ota_active) {
        (void)peak_ota_abort();
      }
      break;
    }
    if (!work_item_is_current(&item)) {
      continue;
    }

    if (item.type == PEAK_BLE_WORK_VESC_RX) {
      bool ota_active;
      taskENTER_CRITICAL(&s_state_lock);
      ota_active = s_state.ota_active;
      taskEXIT_CRITICAL(&s_state_lock);
      if (!ota_active) {
        process_vesc_rx(&item);
      }
    } else {
      process_ota_work(&item);
    }
  }

  taskENTER_CRITICAL(&s_state_lock);
  s_worker_task = NULL;
  taskEXIT_CRITICAL(&s_state_lock);
  xEventGroupSetBits(s_task_events, PEAK_BLE_TASK_WORKER_STOPPED);
  vTaskDelete(NULL);
}

static bool tx_item_is_current(const peak_ble_tx_item_t *item) {
  bool subscribed = false;
  taskENTER_CRITICAL(&s_state_lock);
  if (s_state.running && s_state.conn_handle == item->conn_handle &&
      s_state.conn_generation == item->conn_generation) {
    subscribed = item->type == PEAK_BLE_TX_NUS ? s_state.nus_subscribed
                                               : s_state.ota_status_subscribed;
  }
  taskEXIT_CRITICAL(&s_state_lock);
  return subscribed;
}

static uint16_t connection_payload_size(const peak_ble_tx_item_t *item) {
  uint16_t mtu = BLE_ATT_MTU_DFLT;
  taskENTER_CRITICAL(&s_state_lock);
  if (s_state.conn_handle == item->conn_handle &&
      s_state.conn_generation == item->conn_generation) {
    mtu = s_state.mtu;
  }
  taskEXIT_CRITICAL(&s_state_lock);
  return mtu > 3 ? mtu - 3 : 20;
}

static esp_err_t notify_chunk(const peak_ble_tx_item_t *item,
                              uint16_t value_handle, const uint8_t *data,
                              uint16_t len) {
  for (int attempt = 0; attempt < PEAK_BLE_NOTIFY_RETRIES; attempt++) {
    if (!tx_item_is_current(item)) {
      return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
      (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PEAK_BLE_NOTIFY_WAIT_MS));
      continue;
    }

    int rc = ble_gatts_notify_custom(item->conn_handle, value_handle, om);
    if (rc == 0) {
      return ESP_OK;
    }
    if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY) {
      ESP_LOGW(TAG, "Notification failed: rc=%d", rc);
      return ESP_FAIL;
    }

    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PEAK_BLE_NOTIFY_WAIT_MS));
  }
  return ESP_ERR_TIMEOUT;
}

static void send_tx_item(const peak_ble_tx_item_t *item) {
  if (!tx_item_is_current(item)) {
    return;
  }

  uint16_t value_handle = item->type == PEAK_BLE_TX_NUS
                              ? peak_ble_nus_tx_val_handle
                              : peak_ble_ota_status_val_handle;
  uint16_t max_chunk = connection_payload_size(item);
  size_t offset = 0;

  while (offset < item->len) {
    uint16_t chunk = item->len - offset > max_chunk
                         ? max_chunk
                         : (uint16_t)(item->len - offset);
    esp_err_t err =
        notify_chunk(item, value_handle, item->bytes + offset, chunk);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Dropping remaining notification bytes: %s",
               esp_err_to_name(err));
      return;
    }
    offset += chunk;
  }
}

static void tx_task(void *arg) {
  (void)arg;
  peak_ble_tx_item_t item;

  while (xQueueReceive(s_tx_queue, &item, portMAX_DELAY) == pdTRUE) {
    if (item.type == PEAK_BLE_TX_STOP) {
      break;
    }
    send_tx_item(&item);
  }

  taskENTER_CRITICAL(&s_state_lock);
  s_tx_task = NULL;
  taskEXIT_CRITICAL(&s_state_lock);
  xEventGroupSetBits(s_task_events, PEAK_BLE_TASK_TX_STOPPED);
  vTaskDelete(NULL);
}

static void set_disconnected(uint16_t conn_handle) {
  bool changed = false;
  taskENTER_CRITICAL(&s_state_lock);
  if (s_state.conn_handle == conn_handle ||
      conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    s_state.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_state.mtu = BLE_ATT_MTU_DFLT;
    s_state.nus_subscribed = false;
    s_state.ota_status_subscribed = false;
    s_state.conn_generation++;
    changed = true;
  }
  taskEXIT_CRITICAL(&s_state_lock);

  if (changed) {
    queue_disconnect_abort();
  }
}

static int gap_event(struct ble_gap_event *event, void *arg) {
  (void)arg;

  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      taskENTER_CRITICAL(&s_state_lock);
      s_state.conn_handle = event->connect.conn_handle;
      s_state.mtu = BLE_ATT_MTU_DFLT;
      s_state.nus_subscribed = false;
      s_state.ota_status_subscribed = false;
      s_state.conn_generation++;
      taskEXIT_CRITICAL(&s_state_lock);
      ESP_LOGI(TAG, "BLE client connected (handle=%u)",
               event->connect.conn_handle);
    } else {
      ESP_LOGW(TAG, "BLE connection failed: status=%d", event->connect.status);
      peak_ble_advertise();
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "BLE client disconnected: reason=%d",
             event->disconnect.reason);
    set_disconnected(event->disconnect.conn.conn_handle);
    peak_ble_advertise();
    break;

  case BLE_GAP_EVENT_MTU:
    taskENTER_CRITICAL(&s_state_lock);
    if (s_state.conn_handle == event->mtu.conn_handle) {
      s_state.mtu = event->mtu.value;
    }
    taskEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG, "BLE MTU updated to %u", event->mtu.value);
    break;

  case BLE_GAP_EVENT_SUBSCRIBE: {
    bool send_status = false;
    taskENTER_CRITICAL(&s_state_lock);
    if (s_state.conn_handle == event->subscribe.conn_handle) {
      if (event->subscribe.attr_handle == peak_ble_nus_tx_val_handle) {
        s_state.nus_subscribed = event->subscribe.cur_notify;
      } else if (event->subscribe.attr_handle ==
                 peak_ble_ota_status_val_handle) {
        s_state.ota_status_subscribed = event->subscribe.cur_notify;
        send_status = event->subscribe.cur_notify;
      }
      s_wire_status[3] = status_flags_locked();
    }
    taskEXIT_CRITICAL(&s_state_lock);
    if (send_status) {
      queue_current_status();
    }
    break;
  }

  case BLE_GAP_EVENT_NOTIFY_TX:
    if (s_tx_task != NULL) {
      xTaskNotifyGive(s_tx_task);
    }
    break;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    peak_ble_advertise();
    break;

  default:
    break;
  }
  return 0;
}

static void peak_ble_advertise(void) {
  bool running;
  taskENTER_CRITICAL(&s_state_lock);
  running = s_state.running && s_state.conn_handle == BLE_HS_CONN_HANDLE_NONE;
  taskEXIT_CRITICAL(&s_state_lock);
  if (!running) {
    return;
  }

  struct ble_hs_adv_fields fields = {0};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  fields.tx_pwr_lvl_is_present = 1;
  fields.uuids128 = (ble_uuid128_t *)peak_ble_nus_service_uuid();
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 0;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to set advertising data: rc=%d", rc);
    return;
  }

  struct ble_hs_adv_fields response = {0};
  response.name = (uint8_t *)s_device_name;
  response.name_len = strlen(s_device_name);
  response.name_is_complete = 1;
  response.uuids128 = (ble_uuid128_t *)peak_ble_ota_service_uuid();
  response.num_uuids128 = 1;
  response.uuids128_is_complete = 1;
  rc = ble_gap_adv_rsp_set_fields(&response);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to set scan response: rc=%d", rc);
    return;
  }

  struct ble_gap_adv_params params = {
      .conn_mode = BLE_GAP_CONN_MODE_UND,
      .disc_mode = BLE_GAP_DISC_MODE_GEN,
  };
  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params,
                         gap_event, NULL);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "Failed to start advertising: rc=%d", rc);
  }
}

static void host_on_sync(void) {
  int rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to ensure BLE identity address: rc=%d", rc);
    return;
  }
  rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to infer BLE address type: rc=%d", rc);
    return;
  }

  ESP_LOGI(TAG, "NimBLE synchronized; advertising as %s", s_device_name);
  peak_ble_advertise();
}

static void host_on_reset(int reason) {
  ESP_LOGW(TAG, "NimBLE host reset: reason=%d", reason);
  set_disconnected(BLE_HS_CONN_HANDLE_NONE);
}

static void host_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "NimBLE host task started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static esp_err_t hosted_bt_start(void) {
  esp_err_t err = esp_hosted_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ESP-Hosted init failed: %s", esp_err_to_name(err));
    return err;
  }
  err = esp_hosted_connect_to_slave();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ESP-Hosted connection failed: %s", esp_err_to_name(err));
    return err;
  }
  err = esp_hosted_bt_controller_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Hosted BT controller init failed: %s", esp_err_to_name(err));
    return err;
  }
  err = esp_hosted_bt_controller_enable();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Hosted BT controller enable failed: %s",
             esp_err_to_name(err));
    (void)esp_hosted_bt_controller_deinit(false);
    return err;
  }
  s_hosted_bt_enabled = true;
  return ESP_OK;
}

static void hosted_bt_stop(void) {
  if (!s_hosted_bt_enabled) {
    return;
  }
  esp_err_t err = esp_hosted_bt_controller_disable();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Hosted BT controller disable failed: %s",
             esp_err_to_name(err));
  }
  err = esp_hosted_bt_controller_deinit(false);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Hosted BT controller deinit failed: %s",
             esp_err_to_name(err));
  }
  s_hosted_bt_enabled = false;
}

static void initialize_device_name(void) {
  uint8_t mac[6];
  esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (err == ESP_OK) {
    snprintf(s_device_name, sizeof(s_device_name), "PEAK-%02X%02X", mac[4],
             mac[5]);
  } else {
    ESP_LOGW(TAG, "Could not read MAC for device name: %s",
             esp_err_to_name(err));
    snprintf(s_device_name, sizeof(s_device_name), "PEAK-0000");
  }
}

static esp_err_t start_tasks(void) {
  s_work_queue = xQueueCreateStatic(
      PEAK_BLE_WORK_QUEUE_DEPTH, sizeof(peak_ble_work_item_t),
      s_work_queue_storage, &s_work_queue_control);
  s_tx_queue =
      xQueueCreateStatic(PEAK_BLE_TX_QUEUE_DEPTH, sizeof(peak_ble_tx_item_t),
                         s_tx_queue_storage, &s_tx_queue_control);
  s_task_events = xEventGroupCreateStatic(&s_task_events_storage);
  if (s_work_queue == NULL || s_tx_queue == NULL || s_task_events == NULL) {
    return ESP_ERR_NO_MEM;
  }
  xEventGroupClearBits(s_task_events, PEAK_BLE_TASKS_STOPPED);

  s_worker_task = xTaskCreateStatic(
      worker_task, "ble_work", PEAK_BLE_WORKER_STACK_SIZE, NULL,
      PEAK_BLE_TASK_PRIORITY, s_worker_task_stack, &s_worker_task_control);
  if (s_worker_task == NULL) {
    return ESP_ERR_NO_MEM;
  }
  s_tx_task = xTaskCreateStatic(tx_task, "ble_tx", PEAK_BLE_TX_STACK_SIZE, NULL,
                                PEAK_BLE_TASK_PRIORITY, s_tx_task_stack,
                                &s_tx_task_control);
  if (s_tx_task == NULL) {
    peak_ble_work_item_t stop = {.type = PEAK_BLE_WORK_STOP};
    (void)xQueueSendToFront(s_work_queue, &stop, 0);
    (void)xEventGroupWaitBits(s_task_events, PEAK_BLE_TASK_WORKER_STOPPED,
                              pdFALSE, pdTRUE,
                              pdMS_TO_TICKS(PEAK_BLE_STOP_TIMEOUT_MS));
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

static void stop_tasks(void) {
  if (s_work_queue != NULL && s_worker_task != NULL) {
    peak_ble_work_item_t stop = {.type = PEAK_BLE_WORK_STOP};
    if (xQueueSendToFront(s_work_queue, &stop, 0) != pdTRUE) {
      xQueueReset(s_work_queue);
      (void)xQueueSendToFront(s_work_queue, &stop, 0);
    }
  }
  if (s_tx_queue != NULL && s_tx_task != NULL) {
    peak_ble_tx_item_t stop = {.type = PEAK_BLE_TX_STOP};
    if (xQueueSendToFront(s_tx_queue, &stop, 0) != pdTRUE) {
      xQueueReset(s_tx_queue);
      (void)xQueueSendToFront(s_tx_queue, &stop, 0);
    }
  }

  EventBits_t expected = 0;
  if (s_worker_task != NULL) {
    expected |= PEAK_BLE_TASK_WORKER_STOPPED;
  }
  if (s_tx_task != NULL) {
    expected |= PEAK_BLE_TASK_TX_STOPPED;
  }
  if (expected != 0 && s_task_events != NULL) {
    EventBits_t stopped =
        xEventGroupWaitBits(s_task_events, expected, pdFALSE, pdTRUE,
                            pdMS_TO_TICKS(PEAK_BLE_STOP_TIMEOUT_MS));
    if ((stopped & expected) != expected) {
      ESP_LOGE(TAG, "BLE worker tasks did not stop cleanly");
    }
  }
}

static esp_err_t vesc_transport_start(transport_rx_cb_t rx_cb,
                                      void *user_data) {
  if (rx_cb == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  taskENTER_CRITICAL(&s_state_lock);
  s_state.vesc_rx_cb = rx_cb;
  s_state.vesc_rx_user_data = user_data;
  taskEXIT_CRITICAL(&s_state_lock);
  return ESP_OK;
}

static esp_err_t vesc_transport_send(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > PEAK_BLE_VESC_TX_MAX) {
    return ESP_ERR_INVALID_ARG;
  }

  peak_ble_tx_item_t item = {
      .type = PEAK_BLE_TX_NUS,
      .len = (uint16_t)len,
  };
  taskENTER_CRITICAL(&s_state_lock);
  if (!s_state.running || s_state.conn_handle == BLE_HS_CONN_HANDLE_NONE ||
      !s_state.nus_subscribed || s_state.ota_active) {
    taskEXIT_CRITICAL(&s_state_lock);
    return ESP_ERR_INVALID_STATE;
  }
  item.conn_handle = s_state.conn_handle;
  item.conn_generation = s_state.conn_generation;
  taskEXIT_CRITICAL(&s_state_lock);
  memcpy(item.bytes, data, len);

  return queue_tx_item(&item, false) ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t vesc_transport_stop(void) {
  taskENTER_CRITICAL(&s_state_lock);
  s_state.vesc_rx_cb = NULL;
  s_state.vesc_rx_user_data = NULL;
  taskEXIT_CRITICAL(&s_state_lock);
  return ESP_OK;
}

const transport_iface_t transport_peak_ble = {
    .name = "BLE Bridge",
    .start = vesc_transport_start,
    .send = vesc_transport_send,
    .stop = vesc_transport_stop,
};

esp_err_t peak_ble_start(const peak_ble_config_t *config) {
  taskENTER_CRITICAL(&s_state_lock);
  if (s_state.running) {
    taskEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
  }
  s_state.ota_enabled = config != NULL && config->ota_enabled;
  s_state.conn_handle = BLE_HS_CONN_HANDLE_NONE;
  s_state.mtu = BLE_ATT_MTU_DFLT;
  s_state.nus_subscribed = false;
  s_state.ota_status_subscribed = false;
  s_state.ota_active = false;
  s_state.conn_generation++;
  taskEXIT_CRITICAL(&s_state_lock);

  esp_err_t err = peak_ota_init(NULL, NULL);
  if (err != ESP_OK) {
    return err;
  }
  peak_ota_status_t ota_status;
  if (peak_ota_get_status(&ota_status) == ESP_OK) {
    set_wire_status(0, &ota_status, ESP_OK, false);
  }

  err = start_tasks();
  if (err != ESP_OK) {
    return err;
  }
  initialize_device_name();

  err = hosted_bt_start();
  if (err != ESP_OK) {
    stop_tasks();
    return err;
  }

  err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
    hosted_bt_stop();
    stop_tasks();
    return err;
  }
  s_nimble_initialized = true;

  ble_svc_gap_init();
  ble_svc_gatt_init();
  int rc = ble_svc_gap_device_name_set(s_device_name);
  if (rc == 0) {
    rc = ble_gatts_count_cfg(peak_ble_gatt_services());
  }
  if (rc == 0) {
    rc = ble_gatts_add_svcs(peak_ble_gatt_services());
  }
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to configure static GATT database: rc=%d", rc);
    nimble_port_deinit();
    s_nimble_initialized = false;
    hosted_bt_stop();
    stop_tasks();
    return ESP_FAIL;
  }

  ble_hs_cfg.reset_cb = host_on_reset;
  ble_hs_cfg.sync_cb = host_on_sync;
  taskENTER_CRITICAL(&s_state_lock);
  s_state.running = true;
  taskEXIT_CRITICAL(&s_state_lock);
  nimble_port_freertos_init(host_task);

  ESP_LOGI(TAG, "PEAK BLE started (OTA %s)",
           s_state.ota_enabled ? "enabled" : "disabled");
  return ESP_OK;
}

esp_err_t peak_ble_stop(void) {
  taskENTER_CRITICAL(&s_state_lock);
  bool was_running = s_state.running;
  uint16_t conn_handle = s_state.conn_handle;
  s_state.running = false;
  s_state.nus_subscribed = false;
  s_state.ota_status_subscribed = false;
  taskEXIT_CRITICAL(&s_state_lock);

  if (!was_running && !s_nimble_initialized && !s_hosted_bt_enabled) {
    return ESP_OK;
  }

  stop_tasks();

  if (s_nimble_initialized) {
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
      (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    } else if (ble_gap_adv_active()) {
      (void)ble_gap_adv_stop();
    }

    int rc = nimble_port_stop();
    if (rc != 0) {
      ESP_LOGW(TAG, "NimBLE stop failed: rc=%d", rc);
    }
    nimble_port_deinit();
    s_nimble_initialized = false;
  }
  hosted_bt_stop();

  taskENTER_CRITICAL(&s_state_lock);
  s_state.conn_handle = BLE_HS_CONN_HANDLE_NONE;
  s_state.mtu = BLE_ATT_MTU_DFLT;
  s_state.ota_active = false;
  s_state.conn_generation++;
  taskEXIT_CRITICAL(&s_state_lock);
  ESP_LOGI(TAG, "PEAK BLE stopped");
  return ESP_OK;
}
