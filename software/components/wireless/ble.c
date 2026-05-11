#include "wireless/ble.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_mgr";

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_running = false;
static ble_uuid128_t s_adv_uuid;
static bool s_has_adv_uuid = false;
static const char *s_device_name = "PEAK";
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static bool s_hosted_bt_enabled = false;

static void ble_app_advertise(void);

static esp_err_t ble_hosted_bt_start(void) {
  esp_err_t ret = esp_hosted_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init ESP-Hosted; err=%s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_hosted_connect_to_slave();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to connect to ESP-Hosted co-processor; err=%s",
             esp_err_to_name(ret));
    return ret;
  }

  ret = esp_hosted_bt_controller_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init hosted BT controller; err=%s",
             esp_err_to_name(ret));
    return ret;
  }

  ret = esp_hosted_bt_controller_enable();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable hosted BT controller; err=%s",
             esp_err_to_name(ret));
    esp_hosted_bt_controller_deinit(false);
    return ret;
  }

  s_hosted_bt_enabled = true;
  return ESP_OK;
}

static void ble_hosted_bt_stop(void) {
  if (!s_hosted_bt_enabled) {
    return;
  }

  esp_err_t ret = esp_hosted_bt_controller_disable();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to disable hosted BT controller; err=%s",
             esp_err_to_name(ret));
  }

  ret = esp_hosted_bt_controller_deinit(false);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to deinit hosted BT controller; err=%s",
             esp_err_to_name(ret));
  }

  s_hosted_bt_enabled = false;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      ESP_LOGI(TAG, "Client connected!");
      s_conn_handle = event->connect.conn_handle;
    } else {
      ble_app_advertise();
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "Client disconnected. Resuming advertising.");
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ble_app_advertise();
    break;

  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(TAG, "MTU updated to %d", event->mtu.value);
    break;
  }
  return 0;
}

static void ble_app_advertise(void) {
  if (!s_running)
    return;

  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;

  memset(&fields, 0, sizeof(fields));
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

  fields.name = (uint8_t *)s_device_name;
  fields.name_len = strlen(s_device_name);
  fields.name_is_complete = 1;

  if (s_has_adv_uuid) {
    fields.uuids128 = &s_adv_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
  }

  ble_gap_adv_set_fields(&fields);

  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                    ble_gap_event, NULL);
}

static void ble_app_on_sync(void) {
  int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to infer BLE address type; rc=%d", rc);
    return;
  }

  ble_app_advertise();
}

static void ble_host_task(void *param) {
  ESP_LOGI(TAG, "BLE Host Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
  vTaskDelete(NULL);
}

esp_err_t ble_manager_start(const char *device_name,
                            const struct ble_gatt_svc_def *services,
                            const ble_uuid128_t *adv_uuid) {
  if (s_running)
    return ESP_OK;

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  s_device_name = device_name;
  if (adv_uuid) {
    s_adv_uuid = *adv_uuid;
    s_has_adv_uuid = true;
  }

  ret = ble_hosted_bt_start();
  if (ret != ESP_OK) {
    return ret;
  }

  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init NimBLE host; err=%s", esp_err_to_name(ret));
    ble_hosted_bt_stop();
    return ret;
  }

  ble_svc_gap_device_name_set(s_device_name);
  ble_svc_gap_init();
  ble_svc_gatt_init();

  if (services) {
    ble_gatts_count_cfg(services);
    ble_gatts_add_svcs(services);
  }

  ble_hs_cfg.sync_cb = ble_app_on_sync;
  nimble_port_freertos_init(ble_host_task);

  s_running = true;
  return ESP_OK;
}

uint16_t ble_manager_get_conn_handle(void) { return s_conn_handle; }

esp_err_t ble_manager_stop(void) {
  if (!s_running)
    return ESP_OK;

  if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
  }

  nimble_port_stop();
  nimble_port_deinit();
  ble_hosted_bt_stop();

  s_running = false;
  s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  return ESP_OK;
}
