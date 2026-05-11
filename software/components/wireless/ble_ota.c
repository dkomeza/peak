#include "wireless/ble_ota.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs_mbuf.h"
#include "peak_ota/ota_manager.h"
#include "wireless/ble.h"
#include <string.h>
#include <strings.h>

static const char *TAG = "ble_ota";

// Service: 6E400010-B5A3-F393-E0A9-E50E24DCCA9E
static const ble_uuid128_t s_ota_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x10, 0x00, 0x40, 0x6e);

// Command Write: 6E400011-B5A3-F393-E0A9-E50E24DCCA9E
static const ble_uuid128_t s_ota_cmd_chr_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x11, 0x00, 0x40, 0x6e);

// Status Notify/Read: 6E400012-B5A3-F393-E0A9-E50E24DCCA9E
static const ble_uuid128_t s_ota_status_chr_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x12, 0x00, 0x40, 0x6e);

static uint16_t s_status_val_handle;

static esp_err_t notify_status(const peak_ota_status_t *status) {
  char json[PEAK_OTA_STATUS_JSON_MAX_LEN];
  int len = peak_ota_status_to_json(status, json, sizeof(json));
  if (len <= 0) {
    return ESP_FAIL;
  }
  if (len >= (int)sizeof(json) - 1) {
    len = sizeof(json) - 2;
  }
  json[len++] = '\n';

  uint16_t conn_handle = ble_manager_get_conn_handle();
  if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return ESP_ERR_INVALID_STATE;
  }

  uint16_t mtu = ble_att_mtu(conn_handle);
  uint16_t packet_size = (mtu > 3) ? (mtu - 3) : 20;
  int offset = 0;

  while (offset < len) {
    uint16_t chunk_len =
        (len - offset > packet_size) ? packet_size : (uint16_t)(len - offset);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(json + offset, chunk_len);
    if (om == NULL) {
      return ESP_ERR_NO_MEM;
    }

    if (ble_gatts_notify_custom(conn_handle, s_status_val_handle, om) != 0) {
      return ESP_FAIL;
    }

    offset += chunk_len;
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  return ESP_OK;
}

static void ota_status_cb(const peak_ota_status_t *status, void *user_data) {
  (void)user_data;
  notify_status(status);
}

static void trim_ascii(char *text) {
  size_t len = strlen(text);
  while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n' ||
                     text[len - 1] == ' ' || text[len - 1] == '\t')) {
    text[len - 1] = '\0';
    len--;
  }
}

static esp_err_t send_current_status(void) {
  peak_ota_status_t status;
  esp_err_t ret = peak_ota_get_status(&status);
  if (ret != ESP_OK) {
    return ret;
  }
  return notify_status(&status);
}

static esp_err_t handle_command(const char *command) {
  if (strncasecmp(command, "SET_URL ", 8) == 0) {
    return peak_ota_set_url(command + 8);
  }

  if (strncasecmp(command, "URL ", 4) == 0) {
    return peak_ota_set_url(command + 4);
  }

  if (strcasecmp(command, "START") == 0) {
    return peak_ota_start();
  }

  if (strcasecmp(command, "STATUS") == 0 || strcasecmp(command, "PING") == 0) {
    return send_current_status();
  }

  ESP_LOGW(TAG, "Unknown OTA command: %s", command);
  return ESP_ERR_INVALID_ARG;
}

static int ota_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    peak_ota_status_t status;
    char json[PEAK_OTA_STATUS_JSON_MAX_LEN];
    if (peak_ota_get_status(&status) != ESP_OK) {
      return BLE_ATT_ERR_UNLIKELY;
    }

    int len = peak_ota_status_to_json(&status, json, sizeof(json));
    if (len <= 0 || os_mbuf_append(ctxt->om, json, (uint16_t)len) != 0) {
      return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
  }

  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len >= PEAK_OTA_URL_MAX_LEN + 16) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    char command[PEAK_OTA_URL_MAX_LEN + 16];
    int rc = os_mbuf_copydata(ctxt->om, 0, len, command);
    if (rc != 0) {
      return BLE_ATT_ERR_UNLIKELY;
    }

    command[len] = '\0';
    trim_ascii(command);

    esp_err_t ret = handle_command(command);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "OTA command failed: %s", esp_err_to_name(ret));
      return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
  }

  return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_ota_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_ota_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &s_ota_cmd_chr_uuid.u,
                    .access_cb = ota_chr_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {
                    .uuid = &s_ota_status_chr_uuid.u,
                    .access_cb = ota_chr_access,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_status_val_handle,
                },
                {0}},
    },
    {0}};

esp_err_t ble_ota_start(void) {
  esp_err_t ret = peak_ota_init(ota_status_cb, NULL);
  if (ret != ESP_OK) {
    return ret;
  }

  return ble_manager_start("PEAK", s_ota_svcs, &s_ota_svc_uuid);
}
