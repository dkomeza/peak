#include "ble_bridge.h"
#include "ble.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

// RX (Write): 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
static const ble_uuid128_t gatt_svr_chr_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

// TX (Notify): 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
static const ble_uuid128_t gatt_svr_chr_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t s_tx_val_handle;
static transport_rx_cb_t s_receive_callback = NULL;
static void *s_receive_user_data = NULL;

static esp_err_t ble_bridge_send(const uint8_t *data, size_t len);

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

    if (len > 0) {
      uint8_t buffer[512];
      uint16_t copy_len = (len > sizeof(buffer)) ? sizeof(buffer) : len;

      os_mbuf_copydata(ctxt->om, 0, copy_len, buffer);

      if (copy_len == 4 && memcmp(buffer, "ping", 4) == 0) {
        ble_bridge_send((const uint8_t *)"pong", 4);
      } else if (s_receive_callback) {
        s_receive_callback(buffer, copy_len, s_receive_user_data);
      }
    }
  }
  return 0;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &gatt_svr_chr_rx_uuid.u,
                    .access_cb = gatt_svr_chr_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {
                    .uuid = &gatt_svr_chr_tx_uuid.u,
                    .access_cb = gatt_svr_chr_access,
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_tx_val_handle,
                },
                {0}},
    },
    {0}};

static esp_err_t ble_bridge_start(transport_rx_cb_t rx_cb, void *user_data) {
  s_receive_callback = rx_cb;
  s_receive_user_data = user_data;

  // Inject our custom services into the generic manager
  return ble_manager_start("PEAK", gatt_svr_svcs, &gatt_svr_svc_uuid);
}

static esp_err_t ble_bridge_send(const uint8_t *data, size_t len) {
  uint16_t conn_handle = ble_manager_get_conn_handle();
  if (conn_handle == BLE_HS_CONN_HANDLE_NONE || len == 0) {
    return ESP_ERR_INVALID_STATE;
  }

  uint16_t mtu = ble_att_mtu(conn_handle);
  uint16_t packet_size = (mtu > 3) ? (mtu - 3) : 20;

  size_t offset = 0;
  while (offset < len) {
    uint16_t chunk_len =
        (len - offset > packet_size) ? packet_size : (len - offset);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data + offset, chunk_len);
    if (om == NULL)
      return ESP_ERR_NO_MEM;

    if (ble_gatts_notify_custom(conn_handle, s_tx_val_handle, om) != 0) {
      return ESP_FAIL;
    }

    offset += chunk_len;
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  return ESP_OK;
}

static esp_err_t ble_bridge_stop(void) { return ble_manager_stop(); }

// Map the functions to the generic interface
const transport_iface_t transport_ble = {.name = "BLE Bridge",
                                         .start = ble_bridge_start,
                                         .send = ble_bridge_send,
                                         .stop = ble_bridge_stop};
