#include "ble.h"
#include "esp_log.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
#include "esp_hosted.h"
#endif

static const char *TAG = "ble_mgr";

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;
static bool s_running = false;
static ble_uuid128_t s_adv_uuid;
static bool s_has_adv_uuid = false;
static const char *s_device_name = "ESP32";
#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
static bool s_hosted_bt_controller_started = false;
#endif

static void ble_app_advertise(void);

static void ble_manager_set_log_levels(void) {
  esp_log_level_set("NimBLE", ESP_LOG_WARN);
#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
  esp_log_level_set("H_API", ESP_LOG_WARN);
  esp_log_level_set("H_SDIO_DRV", ESP_LOG_WARN);
  esp_log_level_set("rpc_wrap", ESP_LOG_WARN);
  esp_log_level_set("sdio_wrapper", ESP_LOG_WARN);
  esp_log_level_set("transport", ESP_LOG_WARN);
  esp_log_level_set("vhci_drv", ESP_LOG_WARN);
#endif
}

#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
static esp_err_t ble_manager_start_hosted_controller(void) {
  esp_err_t ret;

  ret = esp_hosted_connect_to_slave();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to connect to ESP-Hosted slave: %s",
             esp_err_to_name(ret));
    return ret;
  }

  ret = esp_hosted_bt_controller_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init hosted BT controller: %s",
             esp_err_to_name(ret));
    return ret;
  }

  ret = esp_hosted_bt_controller_enable();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable hosted BT controller: %s",
             esp_err_to_name(ret));
    esp_hosted_bt_controller_deinit(false);
    return ret;
  }

  s_hosted_bt_controller_started = true;
  return ESP_OK;
}

static void ble_manager_stop_hosted_controller(void) {
  if (!s_hosted_bt_controller_started) {
    return;
  }

  esp_err_t ret = esp_hosted_bt_controller_disable();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to disable hosted BT controller: %s",
             esp_err_to_name(ret));
  }

  ret = esp_hosted_bt_controller_deinit(false);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to deinit hosted BT controller: %s",
             esp_err_to_name(ret));
  }

  s_hosted_bt_controller_started = false;
}
#endif

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
    break;
  }
  return 0;
}

static void ble_app_advertise(void) {
  if (!s_running) {
    ESP_LOGW(TAG, "Ignoring advertise request while BLE manager is stopped");
    return;
  }

  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;
  int rc;

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

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to set BLE advertising data: rc=%d", rc);
    return;
  }

  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                         ble_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to start BLE advertising: rc=%d", rc);
    return;
  }
}

static void ble_app_on_sync(void) {
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

  ble_app_advertise();
}

static void ble_app_on_reset(int reason) {
  ESP_LOGE(TAG, "NimBLE host reset: reason=%d", reason);
}

static void ble_host_task(void *param) {
  nimble_port_run();
  nimble_port_freertos_deinit();
  vTaskDelete(NULL);
}

esp_err_t ble_manager_start(const char *device_name,
                            const struct ble_gatt_svc_def *services,
                            const ble_uuid128_t *adv_uuid) {
  if (s_running)
    return ESP_OK;

  ble_manager_set_log_levels();

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ret = nvs_flash_erase();
    if (ret != ESP_OK) {
      return ret;
    }
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    return ret;
  }

  s_device_name = device_name;
  if (adv_uuid) {
    s_adv_uuid = *adv_uuid;
    s_has_adv_uuid = true;
  }

#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
  ret = ble_manager_start_hosted_controller();
  if (ret != ESP_OK) {
    return ret;
  }
#endif

  ret = nimble_port_init();
  if (ret != ESP_OK) {
#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
    ble_manager_stop_hosted_controller();
#endif
    return ret;
  }

  ble_svc_gap_init();
  ble_svc_gatt_init();

  int rc = ble_svc_gap_device_name_set(s_device_name);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to set BLE GAP name: rc=%d", rc);
    nimble_port_deinit();
#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
    ble_manager_stop_hosted_controller();
#endif
    return ESP_FAIL;
  }

  if (services) {
    rc = ble_gatts_count_cfg(services);
    if (rc != 0) {
      ESP_LOGE(TAG, "Failed to count BLE GATT config: rc=%d", rc);
      nimble_port_deinit();
#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
      ble_manager_stop_hosted_controller();
#endif
      return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(services);
    if (rc != 0) {
      ESP_LOGE(TAG, "Failed to add BLE GATT services: rc=%d", rc);
      nimble_port_deinit();
#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
      ble_manager_stop_hosted_controller();
#endif
      return ESP_FAIL;
    }
  }

  ble_hs_cfg.reset_cb = ble_app_on_reset;
  ble_hs_cfg.sync_cb = ble_app_on_sync;

  s_running = true;
  nimble_port_freertos_init(ble_host_task);
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

  s_running = false;
  s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
  ble_manager_stop_hosted_controller();
#endif
  return ESP_OK;
}
