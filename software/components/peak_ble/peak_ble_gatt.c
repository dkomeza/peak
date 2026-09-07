#include "peak_ble_internal.h"

#include "host/ble_att.h"
#include "host/ble_hs_mbuf.h"
#include "os/os_mbuf.h"
#include <string.h>

/* Nordic UART Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E. */
static const ble_uuid128_t s_nus_service_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

/* NUS RX: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E. */
static const ble_uuid128_t s_nus_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

/* NUS TX: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E. */
static const ble_uuid128_t s_nus_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

/* PEAK OTA service: a6ed0701-d344-460a-8075-b9e8ec90d71b. */
static const ble_uuid128_t s_ota_service_uuid =
    BLE_UUID128_INIT(0x1b, 0xd7, 0x90, 0xec, 0xe8, 0xb9, 0x75, 0x80, 0x0a, 0x46,
                     0x44, 0xd3, 0x01, 0x07, 0xed, 0xa6);

/* OTA control: a6ed0702-d344-460a-8075-b9e8ec90d71b. */
static const ble_uuid128_t s_ota_control_uuid =
    BLE_UUID128_INIT(0x1b, 0xd7, 0x90, 0xec, 0xe8, 0xb9, 0x75, 0x80, 0x0a, 0x46,
                     0x44, 0xd3, 0x02, 0x07, 0xed, 0xa6);

/* OTA data: a6ed0703-d344-460a-8075-b9e8ec90d71b. */
static const ble_uuid128_t s_ota_data_uuid =
    BLE_UUID128_INIT(0x1b, 0xd7, 0x90, 0xec, 0xe8, 0xb9, 0x75, 0x80, 0x0a, 0x46,
                     0x44, 0xd3, 0x03, 0x07, 0xed, 0xa6);

/* OTA status: a6ed0704-d344-460a-8075-b9e8ec90d71b. */
static const ble_uuid128_t s_ota_status_uuid =
    BLE_UUID128_INIT(0x1b, 0xd7, 0x90, 0xec, 0xe8, 0xb9, 0x75, 0x80, 0x0a, 0x46,
                     0x44, 0xd3, 0x04, 0x07, 0xed, 0xa6);

uint16_t peak_ble_nus_tx_val_handle;
uint16_t peak_ble_ota_status_val_handle;

static uint32_t read_le32(const uint8_t *bytes) {
  return ((uint32_t)bytes[0]) | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int enqueue_nus_rx(uint16_t conn_handle,
                          struct ble_gatt_access_ctxt *ctxt) {
  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len == 0 || len > PEAK_BLE_GATT_VALUE_MAX) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  peak_ble_work_item_t item = {
      .type = PEAK_BLE_WORK_VESC_RX,
  };
  item.payload.vesc.len = len;
  if (os_mbuf_copydata(ctxt->om, 0, len, item.payload.vesc.bytes) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return peak_ble_enqueue_work(conn_handle, &item)
             ? 0
             : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int enqueue_ota_control(uint16_t conn_handle,
                               struct ble_gatt_access_ctxt *ctxt) {
  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  uint8_t command[38];

  if (len == 0 || len > sizeof(command) ||
      os_mbuf_copydata(ctxt->om, 0, len, command) != 0) {
    return len > sizeof(command) ? BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN
                                 : BLE_ATT_ERR_UNLIKELY;
  }

  peak_ble_work_item_t item = {0};
  switch (command[0]) {
  case 0x01:
    if (len != sizeof(command) || command[1] != PEAK_BLE_PROTOCOL_VERSION) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    item.type = PEAK_BLE_WORK_OTA_BEGIN;
    item.payload.ota_begin.image_size = read_le32(&command[2]);
    memcpy(item.payload.ota_begin.sha256, &command[6],
           sizeof(item.payload.ota_begin.sha256));
    break;

  case 0x02:
    if (len != 1) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    item.type = PEAK_BLE_WORK_OTA_FINISH;
    break;

  case 0x03:
    if (len != 1) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    item.type = PEAK_BLE_WORK_OTA_ABORT;
    break;

  case 0x04:
    if (len != 1) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    item.type = PEAK_BLE_WORK_OTA_QUERY;
    break;

  default:
    return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
  }

  return peak_ble_enqueue_work(conn_handle, &item)
             ? 0
             : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int enqueue_ota_data(uint16_t conn_handle,
                            struct ble_gatt_access_ctxt *ctxt) {
  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  uint8_t value[PEAK_BLE_GATT_VALUE_MAX];
  if (len < 5 || len > sizeof(value)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  if (os_mbuf_copydata(ctxt->om, 0, len, value) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  peak_ble_work_item_t item = {
      .type = PEAK_BLE_WORK_OTA_DATA,
  };
  item.payload.ota_data.offset = read_le32(value);
  item.payload.ota_data.len = len - sizeof(uint32_t);
  memcpy(item.payload.ota_data.bytes, value + sizeof(uint32_t),
         item.payload.ota_data.len);

  return peak_ble_enqueue_work(conn_handle, &item)
             ? 0
             : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int nus_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }
  return enqueue_nus_rx(conn_handle, ctxt);
}

static int ota_control_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }
  return enqueue_ota_control(conn_handle, ctxt);
}

static int ota_data_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }
  return enqueue_ota_data(conn_handle, ctxt);
}

static int ota_status_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  uint8_t status[PEAK_BLE_STATUS_LEN];
  peak_ble_copy_wire_status(status);
  return os_mbuf_append(ctxt->om, status, sizeof(status)) == 0
             ? 0
             : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_chr_def s_nus_characteristics[] = {
    {
        .uuid = &s_nus_rx_uuid.u,
        .access_cb = nus_rx_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &s_nus_tx_uuid.u,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &peak_ble_nus_tx_val_handle,
    },
    {0},
};

static const struct ble_gatt_chr_def s_ota_characteristics[] = {
    {
        .uuid = &s_ota_control_uuid.u,
        .access_cb = ota_control_access,
        .flags = BLE_GATT_CHR_F_WRITE,
    },
    {
        .uuid = &s_ota_data_uuid.u,
        .access_cb = ota_data_access,
        .flags = BLE_GATT_CHR_F_WRITE,
    },
    {
        .uuid = &s_ota_status_uuid.u,
        .access_cb = ota_status_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &peak_ble_ota_status_val_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_nus_service_uuid.u,
        .characteristics = s_nus_characteristics,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_ota_service_uuid.u,
        .characteristics = s_ota_characteristics,
    },
    {0},
};

const struct ble_gatt_svc_def *peak_ble_gatt_services(void) {
  return s_services;
}

const ble_uuid128_t *peak_ble_nus_service_uuid(void) {
  return &s_nus_service_uuid;
}

const ble_uuid128_t *peak_ble_ota_service_uuid(void) {
  return &s_ota_service_uuid;
}
