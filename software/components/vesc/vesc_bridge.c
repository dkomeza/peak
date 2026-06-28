#include "vesc/vesc_bridge.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <string.h>

#include "connection/can.h"
#include "crc.h"
#include "vesc/packet.h"
#include "vesc/transport_iface.h"

static const char *TAG = "vesc_bridge";

#define BRIDGE_CAN_ID 254
#define TARGET_VESC_CAN_ID 69
#define VESC_PAYLOAD_MAX_LEN PACKET_MAX_PL_LEN
#define CAN_RX_BUFFER_COUNT 3
#define VESC_BRIDGE_MAX_TRANSPORTS 2
#define ACTIVE_TRANSPORT_TIMEOUT_MS 5000

typedef struct {
  const transport_iface_t *iface;
  PACKET_STATE_t rx_packet_state;
  bool started;
} vesc_transport_route_t;

typedef enum {
  CAN_PACKET_FILL_RX_BUFFER = 5,
  CAN_PACKET_FILL_RX_BUFFER_LONG = 6,
  CAN_PACKET_PROCESS_RX_BUFFER = 7,
  CAN_PACKET_PROCESS_SHORT_BUFFER = 8,
} vesc_can_packet_t;

static PACKET_STATE_t s_transport_tx_packet_state;
static vesc_transport_route_t s_routes[VESC_BRIDGE_MAX_TRANSPORTS];
static size_t s_route_count;
static vesc_transport_route_t *s_active_route;
static vesc_transport_route_t *s_processing_rx_route;
static vesc_transport_route_t *s_sending_tx_route;
static TickType_t s_active_last_rx_tick;
static SemaphoreHandle_t s_state_mutex;
static uint8_t s_can_rx_buffers[CAN_RX_BUFFER_COUNT][VESC_PAYLOAD_MAX_LEN];
static uint16_t s_can_rx_offsets[CAN_RX_BUFFER_COUNT];
static bool s_initialized;
static bool s_started;
static bool s_can_registered;

// Telemetry counters
static uint32_t s_invalid_can_responses;
static uint32_t s_transport_send_failures;
static uint32_t s_can_send_failures;

static void log_bridge_drops(void) {
  ESP_LOGW(TAG,
           "Bridge drops: invalid_can=%" PRIu32 ", transport_send=%" PRIu32
           ", can_send=%" PRIu32,
           s_invalid_can_responses, s_transport_send_failures,
           s_can_send_failures);
}

static esp_err_t send_can_packet(uint8_t target_id, vesc_can_packet_t packet_id,
                                 const uint8_t *data, uint8_t len) {
  uint32_t id = target_id | ((uint32_t)packet_id << 8);

  ESP_LOGD(TAG, "ESP->CAN, id: %lu, len: %d", id, len);

  esp_err_t ret = can_send(id, data, len, 0);
  if (ret != ESP_OK) {
    s_can_send_failures++;
    log_bridge_drops();
  }
  return ret;
}

static esp_err_t send_payload_over_can(const uint8_t *data, unsigned int len) {
  uint8_t frame_data[8];

  if (data == NULL || len == 0 || len > VESC_PAYLOAD_MAX_LEN) {
    return ESP_ERR_INVALID_ARG;
  }

  if (len <= 6) {
    frame_data[0] = BRIDGE_CAN_ID;
    frame_data[1] = 0;
    memcpy(frame_data + 2, data, len);
    return send_can_packet(TARGET_VESC_CAN_ID, CAN_PACKET_PROCESS_SHORT_BUFFER,
                           frame_data, len + 2);
  }

  unsigned int offset = 0;
  while (offset < len && offset <= UINT8_MAX) {
    uint8_t chunk_len = (len - offset > 7) ? 7 : (len - offset);

    frame_data[0] = (uint8_t)offset;
    memcpy(frame_data + 1, data + offset, chunk_len);

    esp_err_t ret =
        send_can_packet(TARGET_VESC_CAN_ID, CAN_PACKET_FILL_RX_BUFFER,
                        frame_data, chunk_len + 1);
    if (ret != ESP_OK)
      return ret;

    offset += chunk_len;
  }

  while (offset < len) {
    uint8_t chunk_len = (len - offset > 6) ? 6 : (len - offset);

    frame_data[0] = (uint8_t)(offset >> 8);
    frame_data[1] = (uint8_t)(offset & 0xFF);
    memcpy(frame_data + 2, data + offset, chunk_len);

    esp_err_t ret =
        send_can_packet(TARGET_VESC_CAN_ID, CAN_PACKET_FILL_RX_BUFFER_LONG,
                        frame_data, chunk_len + 2);
    if (ret != ESP_OK)
      return ret;

    offset += chunk_len;
  }

  unsigned short crc = crc16((unsigned char *)data, len);
  frame_data[0] = BRIDGE_CAN_ID;
  frame_data[1] = 0;
  frame_data[2] = (uint8_t)(len >> 8);
  frame_data[3] = (uint8_t)(len & 0xFF);
  frame_data[4] = (uint8_t)(crc >> 8);
  frame_data[5] = (uint8_t)(crc & 0xFF);

  return send_can_packet(TARGET_VESC_CAN_ID, CAN_PACKET_PROCESS_RX_BUFFER,
                         frame_data, 6);
}

static void reset_can_rx_buffers(void) {
  memset(s_can_rx_offsets, 0, sizeof(s_can_rx_offsets));
}

static int find_can_rx_buffer(uint16_t offset) {
  for (int i = 0; i < CAN_RX_BUFFER_COUNT; i++) {
    if (s_can_rx_offsets[i] == offset)
      return i;
  }
  return -1;
}

static bool append_can_response(uint16_t offset, const uint8_t *data,
                                uint8_t len) {
  int buffer_index = find_can_rx_buffer(offset);
  if (buffer_index < 0 || ((uint32_t)offset + len) > VESC_PAYLOAD_MAX_LEN) {
    s_invalid_can_responses++;
    log_bridge_drops();
    return false;
  }

  memcpy(s_can_rx_buffers[buffer_index] + offset, data, len);
  s_can_rx_offsets[buffer_index] = offset + len;
  return true;
}

static bool active_route_is_fresh(TickType_t now) {
  if (s_active_route == NULL) {
    return false;
  }

  TickType_t timeout_ticks = pdMS_TO_TICKS(ACTIVE_TRANSPORT_TIMEOUT_MS);
  return (now - s_active_last_rx_tick) <= timeout_ticks;
}

static vesc_transport_route_t *get_active_route(void) {
  vesc_transport_route_t *route = NULL;

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (active_route_is_fresh(xTaskGetTickCount())) {
    route = s_active_route;
  } else {
    s_active_route = NULL;
  }
  xSemaphoreGive(s_state_mutex);

  return route;
}

static void send_transport_packet_bytes(unsigned char *data, unsigned int len) {
  vesc_transport_route_t *route = s_sending_tx_route;

  if (route == NULL || route->iface == NULL) {
    return;
  }

  ESP_LOGD(TAG, "ESP->%s, len: %d", route->iface->name, len);
  if (route->iface->send((const uint8_t *)data, len) != ESP_OK) {
    s_transport_send_failures++;
    log_bridge_drops();
  }
}

static void forward_can_payload_to_active_transport(const uint8_t *data,
                                                    unsigned int len) {
  vesc_transport_route_t *route = get_active_route();
  if (route == NULL) {
    s_transport_send_failures++;
    ESP_LOGW(TAG, "Dropping CAN response with no active VESC transport");
    log_bridge_drops();
    return;
  }

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_sending_tx_route = route;
  packet_send_packet((unsigned char *)data, len, &s_transport_tx_packet_state);
  s_sending_tx_route = NULL;
  xSemaphoreGive(s_state_mutex);
}

static void handle_can_frame(uint32_t id, const uint8_t *data, uint8_t len,
                             void *user_data) {
  (void)user_data;

  if (!s_started) {
    return;
  }

  uint8_t packet_id = id >> 8;
  ESP_LOGD(TAG, "CAN->ESP, packet_id: %u, len: %d", packet_id, len);

  switch (packet_id) {
  case CAN_PACKET_FILL_RX_BUFFER:
    if (len < 1)
      break;
    append_can_response(data[0], data + 1, len - 1);
    return;

  case CAN_PACKET_FILL_RX_BUFFER_LONG:
    if (len < 2)
      break;
    append_can_response(((uint16_t)data[0] << 8) | data[1], data + 2, len - 2);
    return;

  case CAN_PACKET_PROCESS_RX_BUFFER: {
    if (len < 6)
      break;

    uint16_t rx_len = ((uint16_t)data[2] << 8) | data[3];
    unsigned short rx_crc = ((unsigned short)data[4] << 8) | data[5];

    if (rx_len > VESC_PAYLOAD_MAX_LEN)
      break;

    int buffer_index = find_can_rx_buffer(rx_len);
    if (buffer_index < 0)
      break;

    if (crc16(s_can_rx_buffers[buffer_index], rx_len) != rx_crc) {
      s_can_rx_offsets[buffer_index] = 0;
      break;
    }

    forward_can_payload_to_active_transport(s_can_rx_buffers[buffer_index],
                                            rx_len);
    s_can_rx_offsets[buffer_index] = 0;
    return;
  }

  case CAN_PACKET_PROCESS_SHORT_BUFFER:
    if (len <= 2)
      break;
    forward_can_payload_to_active_transport(data + 2, len - 2);
    return;

  default:
    return;
  }

  // If we hit break statements above, it's an invalid packet
  s_invalid_can_responses++;
  log_bridge_drops();
}

static void handle_transport_payload(unsigned char *data, unsigned int len) {
  vesc_transport_route_t *route = s_processing_rx_route;
  if (route == NULL || route->iface == NULL) {
    return;
  }

  s_active_route = route;
  s_active_last_rx_tick = xTaskGetTickCount();

  ESP_LOGD(TAG, "%s->CAN payload, len: %d", route->iface->name, len);
  esp_err_t ret = send_payload_over_can(data, len);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to forward %s payload over CAN: %s",
             route->iface->name, esp_err_to_name(ret));
  }
}

static void handle_transport_bytes(const uint8_t *data, size_t len,
                                   void *user_data) {
  vesc_transport_route_t *route = (vesc_transport_route_t *)user_data;
  if (route == NULL || !route->started || data == NULL || len == 0) {
    return;
  }

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_processing_rx_route = route;
  for (size_t i = 0; i < len; i++) {
    packet_process_byte(data[i], &route->rx_packet_state);
  }
  s_processing_rx_route = NULL;
  xSemaphoreGive(s_state_mutex);
}

esp_err_t vesc_bridge_init(void) {
  if (s_initialized)
    return ESP_OK;

  s_state_mutex = xSemaphoreCreateMutex();
  if (s_state_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  packet_init(send_transport_packet_bytes, NULL, &s_transport_tx_packet_state);

  reset_can_rx_buffers();
  s_initialized = true;

  ESP_LOGI(TAG, "VESC bridge initialized: local CAN id %u, target CAN id %u",
           BRIDGE_CAN_ID, TARGET_VESC_CAN_ID);
  return ESP_OK;
}

esp_err_t vesc_bridge_start(const transport_iface_t *const *transports,
                            size_t transport_count) {
  if (!s_initialized || transports == NULL || transport_count == 0)
    return ESP_ERR_INVALID_STATE;

  if (s_started)
    return ESP_OK;

  if (transport_count > VESC_BRIDGE_MAX_TRANSPORTS) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(s_routes, 0, sizeof(s_routes));
  s_route_count = transport_count;
  for (size_t i = 0; i < transport_count; i++) {
    if (transports[i] == NULL || transports[i]->start == NULL ||
        transports[i]->send == NULL) {
      s_route_count = 0;
      return ESP_ERR_INVALID_ARG;
    }

    s_routes[i].iface = transports[i];
    packet_init(NULL, handle_transport_payload, &s_routes[i].rx_packet_state);
  }

  size_t started_count = 0;
  esp_err_t first_error = ESP_OK;
  for (size_t i = 0; i < s_route_count; i++) {
    esp_err_t ret =
        s_routes[i].iface->start(handle_transport_bytes, &s_routes[i]);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to start transport '%s': %s",
               s_routes[i].iface->name, esp_err_to_name(ret));
      if (first_error == ESP_OK) {
        first_error = ret;
      }
      continue;
    }

    s_routes[i].started = true;
    started_count++;
  }

  if (started_count == 0) {
    s_route_count = 0;
    return first_error != ESP_OK ? first_error : ESP_ERR_INVALID_STATE;
  }

  if (!s_can_registered) {
    esp_err_t ret = can_register_cb(BRIDGE_CAN_ID, 0xFF, handle_can_frame, NULL);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to register CAN callback");

      for (size_t i = 0; i < s_route_count; i++) {
        if (s_routes[i].started && s_routes[i].iface->stop != NULL) {
          s_routes[i].iface->stop();
        }
      }
      memset(s_routes, 0, sizeof(s_routes));
      s_route_count = 0;
      return ret;
    }
    s_can_registered = true;
  }

  s_started = true;
  ESP_LOGI(TAG, "VESC bridge started with %u transport(s)",
           (unsigned int)started_count);
  return ESP_OK;
}

esp_err_t vesc_bridge_stop(void) {
  if (!s_initialized || !s_started) {
    return ESP_OK;
  }

  s_started = false;

  esp_err_t first_error = ESP_OK;
  for (size_t i = 0; i < s_route_count; i++) {
    if (s_routes[i].started && s_routes[i].iface != NULL &&
        s_routes[i].iface->stop != NULL) {
      esp_err_t ret = s_routes[i].iface->stop();
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Transport '%s' failed to stop cleanly: %s",
                 s_routes[i].iface->name, esp_err_to_name(ret));
        if (first_error == ESP_OK) {
          first_error = ret;
        }
      }
    }
  }

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_active_route = NULL;
  s_processing_rx_route = NULL;
  s_sending_tx_route = NULL;
  xSemaphoreGive(s_state_mutex);

  memset(s_routes, 0, sizeof(s_routes));
  s_route_count = 0;

  if (first_error != ESP_OK) {
    return first_error;
  }

  ESP_LOGI(TAG, "VESC bridge stopped gracefully");
  return ESP_OK;
}
