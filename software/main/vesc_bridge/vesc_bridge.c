#include "vesc_bridge.h"

#include "can.h"
#include "transport_iface.h"

#include "crc.h"
#include "esp_log.h"
#include "packet.h"
#include <inttypes.h>
#include <string.h>

static const char *TAG = "vesc_bridge";

#define BRIDGE_CAN_ID 254
#define TARGET_VESC_CAN_ID 69
#define VESC_PAYLOAD_MAX_LEN PACKET_MAX_PL_LEN
#define CAN_RX_BUFFER_COUNT 3

static const transport_iface_t *s_transport = NULL;

typedef enum {
  CAN_PACKET_FILL_RX_BUFFER = 5,
  CAN_PACKET_FILL_RX_BUFFER_LONG = 6,
  CAN_PACKET_PROCESS_RX_BUFFER = 7,
  CAN_PACKET_PROCESS_SHORT_BUFFER = 8,
} vesc_can_packet_t;

static PACKET_STATE_t s_transport_rx_packet_state;
static PACKET_STATE_t s_transport_tx_packet_state;
static uint8_t s_can_rx_buffers[CAN_RX_BUFFER_COUNT][VESC_PAYLOAD_MAX_LEN];
static uint16_t s_can_rx_offsets[CAN_RX_BUFFER_COUNT];
static bool s_initialized;
static bool s_started;

// Telemetry counters
static uint32_t s_invalid_can_responses;
static uint32_t s_transport_send_failures;
static uint32_t s_can_send_failures;

static void log_bridge_drops(void) {
  ESP_LOGW(TAG,
           "Bridge drops: invalid_can=%" PRIu32 ", udp_send=%" PRIu32
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

static void forward_can_payload_to_udp(const uint8_t *data, unsigned int len) {
  packet_send_packet((unsigned char *)data, len, &s_transport_tx_packet_state);
}

static void handle_udp_payload(unsigned char *data, unsigned int len) {
  ESP_LOGD(TAG, "Full UDP Payload Rx, len: %d", len);
  esp_err_t ret = send_payload_over_can(data, len);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to forward UDP payload over CAN: %s",
             esp_err_to_name(ret));
  }
}

static void handle_udp_bytes(const uint8_t *data, size_t len, void *user_data) {
  (void)user_data;

  for (size_t i = 0; i < len; i++) {
    packet_process_byte(data[i], &s_transport_rx_packet_state);
  }
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

    forward_can_payload_to_udp(s_can_rx_buffers[buffer_index], rx_len);
    s_can_rx_offsets[buffer_index] = 0;
    return;
  }

  case CAN_PACKET_PROCESS_SHORT_BUFFER:
    if (len <= 2)
      break;
    forward_can_payload_to_udp(data + 2, len - 2);
    return;

  default:
    return;
  }

  // If we hit break statements above, it's an invalid packet
  s_invalid_can_responses++;
  log_bridge_drops();
}

static void send_transport_packet_bytes(unsigned char *data, unsigned int len) {
  if (s_transport == NULL)
    return;

  ESP_LOGD(TAG, "ESP->%s, len: %d", s_transport->name, len);
  if (s_transport->send((const uint8_t *)data, len) != ESP_OK) {
    s_transport_send_failures++;
    log_bridge_drops();
  }
}

static void handle_transport_payload(unsigned char *data, unsigned int len) {
  ESP_LOGD(TAG, "Full Transport Payload Rx, len: %d", len);
  esp_err_t ret = send_payload_over_can(data, len);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to forward payload over CAN: %s",
             esp_err_to_name(ret));
  }
}

static void handle_transport_bytes(const uint8_t *data, size_t len,
                                   void *user_data) {
  (void)user_data;

  for (size_t i = 0; i < len; i++) {
    packet_process_byte(data[i], &s_transport_rx_packet_state);
  }
}

esp_err_t vesc_bridge_init(void) {
  if (s_initialized)
    return ESP_OK;

  // Now properly using generic transport names!
  packet_init(NULL, handle_transport_payload, &s_transport_rx_packet_state);
  packet_init(send_transport_packet_bytes, NULL, &s_transport_tx_packet_state);

  reset_can_rx_buffers();
  s_initialized = true;

  ESP_LOGI(TAG, "VESC bridge initialized: local CAN id %u, target CAN id %u",
           BRIDGE_CAN_ID, TARGET_VESC_CAN_ID);
  return ESP_OK;
}

esp_err_t vesc_bridge_start(const transport_iface_t *transport) {
  if (!s_initialized || transport == NULL)
    return ESP_ERR_INVALID_STATE;

  if (s_started)
    return ESP_OK;

  s_transport = transport;

  esp_err_t ret = s_transport->start(handle_transport_bytes, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start transport '%s'", s_transport->name);
    s_transport = NULL;
    return ret;
  }

  // 2. Try to hook into the CAN bus
  ret = can_register_cb(BRIDGE_CAN_ID, 0xFF, handle_can_frame, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register CAN callback");

    // Unroll the successful transport start to prevent a zombie state
    if (s_transport->stop != NULL) {
      s_transport->stop();
    }
    s_transport = NULL;
    return ret;
  }

  s_started = true;
  ESP_LOGI(TAG, "VESC bridge started using %s", s_transport->name);
  return ESP_OK;
}

esp_err_t vesc_bridge_stop(void) {
  if (!s_initialized || !s_started) {
    return ESP_OK;
  }

  s_started = false;

  if (s_transport != NULL && s_transport->stop != NULL) {
    esp_err_t ret = s_transport->stop();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Transport '%s' failed to stop cleanly: %s",
               s_transport->name, esp_err_to_name(ret));
      return ret;
    }
  }

  s_transport = NULL;

  ESP_LOGI(TAG, "VESC bridge stopped gracefully");
  return ESP_OK;
}
