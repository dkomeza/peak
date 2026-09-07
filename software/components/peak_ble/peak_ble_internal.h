#ifndef PEAK_BLE_INTERNAL_H
#define PEAK_BLE_INTERNAL_H

#include "host/ble_gatt.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PEAK_BLE_PROTOCOL_VERSION 1
#define PEAK_BLE_STATUS_LEN 16
#define PEAK_BLE_GATT_VALUE_MAX 253
#define PEAK_BLE_OTA_DATA_MAX (PEAK_BLE_GATT_VALUE_MAX - sizeof(uint32_t))

typedef enum {
  PEAK_BLE_OTA_ERROR_NONE = 0,
  PEAK_BLE_OTA_ERROR_NOT_AUTHORIZED = 1,
  PEAK_BLE_OTA_ERROR_INVALID_STATE = 2,
  PEAK_BLE_OTA_ERROR_INVALID_ARGUMENT = 3,
  PEAK_BLE_OTA_ERROR_INVALID_SIZE = 4,
  PEAK_BLE_OTA_ERROR_DIGEST_MISMATCH = 5,
  PEAK_BLE_OTA_ERROR_NO_UPDATE_PARTITION = 6,
  PEAK_BLE_OTA_ERROR_RESOURCE_EXHAUSTED = 7,
  PEAK_BLE_OTA_ERROR_TIMEOUT = 8,
  PEAK_BLE_OTA_ERROR_INTERNAL = 127,
} peak_ble_ota_error_t;

typedef enum {
  PEAK_BLE_WORK_VESC_RX = 0,
  PEAK_BLE_WORK_OTA_BEGIN,
  PEAK_BLE_WORK_OTA_DATA,
  PEAK_BLE_WORK_OTA_FINISH,
  PEAK_BLE_WORK_OTA_ABORT,
  PEAK_BLE_WORK_OTA_QUERY,
  PEAK_BLE_WORK_STOP,
} peak_ble_work_type_t;

typedef struct {
  peak_ble_work_type_t type;
  uint16_t conn_handle;
  uint32_t conn_generation;
  union {
    struct {
      uint16_t len;
      uint8_t bytes[PEAK_BLE_GATT_VALUE_MAX];
    } vesc;
    struct {
      uint32_t image_size;
      uint8_t sha256[32];
    } ota_begin;
    struct {
      uint32_t offset;
      uint16_t len;
      uint8_t bytes[PEAK_BLE_OTA_DATA_MAX];
    } ota_data;
  } payload;
} peak_ble_work_item_t;

extern uint16_t peak_ble_nus_tx_val_handle;
extern uint16_t peak_ble_ota_status_val_handle;

const struct ble_gatt_svc_def *peak_ble_gatt_services(void);
const ble_uuid128_t *peak_ble_nus_service_uuid(void);
const ble_uuid128_t *peak_ble_ota_service_uuid(void);

bool peak_ble_enqueue_work(uint16_t conn_handle, peak_ble_work_item_t *item);
void peak_ble_copy_wire_status(uint8_t status[PEAK_BLE_STATUS_LEN]);

#endif
