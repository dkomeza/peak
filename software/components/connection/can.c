#include "can.h"

#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
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

static can_subscriber_t s_subscribers[CAN_MAX_CALLBACKS];
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

static void can_rx_dispatcher_task(void *arg) {
  twai_message_t rx_msg;

  while (1) {
    if (twai_receive(&rx_msg, portMAX_DELAY) == ESP_OK) {
      if (!rx_msg.extd || rx_msg.rtr) {
        continue;
      }

      if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < CAN_MAX_CALLBACKS; i++) {
          can_subscriber_t *sub = &s_subscribers[i];

          if (sub->active && sub->cb != NULL) {
            if ((rx_msg.identifier & sub->mask) == sub->id) {
              sub->cb(rx_msg.identifier, rx_msg.data, rx_msg.data_length_code,
                      sub->user_data);
            }
          }
        }
        xSemaphoreGive(s_mutex);
      }
    }
  }
}

esp_err_t can_init(void) {
  if (s_initialized)
    return ESP_OK;

  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL)
    return ESP_ERR_NO_MEM;

  memset(s_subscribers, 0, sizeof(s_subscribers));

  twai_general_config_t g_config =
      TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
  g_config.rx_queue_len = CAN_RX_QUEUE_DEPTH;
  g_config.tx_queue_len = CAN_TX_QUEUE_DEPTH;

  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
  if (ret != ESP_OK)
    return ret;

  ret = twai_start();
  if (ret != ESP_OK)
    return ret;

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

esp_err_t can_send(uint32_t id, const uint8_t *data, uint8_t len,
                   uint16_t timeout_ms) {
  if (len > 8 || (data == NULL && len > 0))
    return ESP_ERR_INVALID_ARG;

  twai_message_t tx_msg = {
      .extd = 1, .rtr = 0, .ss = 0, .identifier = id, .data_length_code = len};

  if (len > 0)
    memcpy(tx_msg.data, data, len);

  return twai_transmit(&tx_msg, pdMS_TO_TICKS(timeout_ms));
}
