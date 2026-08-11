#include "can.h"

#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_twai_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "can_bus";

#define CAN_RX_GPIO GPIO_NUM_21
#define CAN_TX_GPIO GPIO_NUM_22
#define CAN_RX_QUEUE_DEPTH 32
#define CAN_TX_QUEUE_DEPTH 128
#define CAN_MAX_CALLBACKS 8

typedef struct {
  uint32_t id;
  uint32_t mask;
  can_bus_receive_cb_t cb;
  void *user_data;
  bool active;
} can_subscriber_t;

typedef struct {
  twai_frame_header_t header;
  uint8_t buffer[8];
} can_rx_msg_t;

static can_subscriber_t s_subscribers[CAN_MAX_CALLBACKS];
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;
static twai_node_handle_t s_node_hdl = NULL;
static QueueHandle_t s_rx_queue = NULL;

static void can_rx_dispatcher_task(void *arg) {
  twai_frame_t rx_msg;

  while (1) {
    if (xQueueReceive(s_rx_queue, &rx_msg, portMAX_DELAY) == ESP_OK) {
      if (!rx_msg.header.ide || rx_msg.header.rtr) {
        continue;
      }

      if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < CAN_MAX_CALLBACKS; i++) {
          can_subscriber_t *sub = &s_subscribers[i];

          if (sub->active && sub->cb != NULL) {
            if ((rx_msg.header.id & sub->mask) == sub->id) {
              sub->cb(rx_msg.header.id, rx_msg.buffer, rx_msg.header.dlc,
                      sub->user_data);
            }
          }
        }
        xSemaphoreGive(s_mutex);
      }
    }
  }
}

static bool can_on_rx_done(twai_node_handle_t node,
                           const twai_rx_done_event_data_t *edata,
                           void *user_ctx) {
  can_rx_msg_t msg;
  BaseType_t higher_priority_task_woken = pdFALSE;

  twai_frame_t rx_msg = {
      .buffer = msg.buffer,
      .buffer_len = sizeof(msg.buffer),
  };

  if (ESP_OK == twai_node_receive_from_isr(node, &rx_msg)) {
    msg.header = rx_msg.header;
    xQueueSendFromISR(s_rx_queue, &msg, &higher_priority_task_woken);
  }

  return (higher_priority_task_woken == pdTRUE);
}

esp_err_t can_init(void) {
  if (s_initialized)
    return ESP_OK;

  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL)
    return ESP_ERR_NO_MEM;

  memset(s_subscribers, 0, sizeof(s_subscribers));

  twai_onchip_node_config_t node_config = {
      .io_cfg.tx = CAN_TX_GPIO,
      .io_cfg.rx = CAN_RX_GPIO,
      .bit_timing.bitrate = 500000, // 500 kbps
      .tx_queue_depth = CAN_TX_QUEUE_DEPTH,
  };

  esp_err_t node_ret = twai_new_node_onchip(&node_config, &s_node_hdl);
  if (node_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create TWAI node: %s", esp_err_to_name(node_ret));
    return node_ret;
  }

  s_rx_queue = xQueueCreate(CAN_RX_QUEUE_DEPTH, sizeof(can_rx_msg_t));
  twai_event_callbacks_t event_cbs = {
      .on_rx_done = can_on_rx_done,
  };
  esp_err_t event_ret =
      twai_node_register_event_callbacks(s_node_hdl, &event_cbs, NULL);
  if (event_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register TWAI event callbacks: %s",
             esp_err_to_name(event_ret));
    return event_ret;
  }

  esp_err_t start_ret = twai_node_enable(s_node_hdl);
  if (start_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable TWAI node: %s", esp_err_to_name(start_ret));
    return start_ret;
  }

  if (xTaskCreate(can_rx_dispatcher_task, "can_dispatch", 4096, NULL, 5,
                  NULL) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "CAN Dispatcher initialized");
  return ESP_OK;
}

esp_err_t can_register_cb(uint32_t id, uint32_t mask, can_bus_receive_cb_t cb,
                          void *user_data) {
  if (!s_initialized || cb == NULL)
    return ESP_ERR_INVALID_STATE;

  esp_err_t ret = ESP_ERR_NOT_FOUND; // Default to "no slots left"

  if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < CAN_MAX_CALLBACKS; i++) {
      if (!s_subscribers[i].active) {
        s_subscribers[i].id = id;
        s_subscribers[i].mask = mask;
        s_subscribers[i].cb = cb;
        s_subscribers[i].user_data = user_data;
        s_subscribers[i].active = true;
        ret = ESP_OK;
        break;
      }
    }
    xSemaphoreGive(s_mutex);
  }
  return ret;
}

esp_err_t can_send(uint32_t id, uint8_t *data, uint8_t len,
                   uint16_t timeout_ms) {
  if (len > 8 || (data == NULL && len > 0))
    return ESP_ERR_INVALID_ARG;

  twai_frame_t tx_msg = {
      .header.id = id,
      .header.ide = true,
      .header.rtr = false,
      .header.dlc = len,

      .buffer = data,
      .buffer_len = len,
  };

  return twai_node_transmit(s_node_hdl, &tx_msg, pdMS_TO_TICKS(timeout_ms));
}
