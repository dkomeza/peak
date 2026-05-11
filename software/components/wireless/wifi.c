#include "wireless/wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi_mgr";

#define WIFI_AP_PASSWORD "peak1234"
#define WIFI_AP_MAX_CONNECTIONS 1

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Connected! IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event =
        (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac),
             event->aid);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event =
        (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event->mac),
             event->aid);
  }
}

static void wifi_init_common(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
}

static void wifi_init_driver(void) {
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
}

esp_err_t wifi_start(const char *ssid, const char *password) {
  wifi_init_common();
  esp_netif_create_default_wifi_sta();
  wifi_init_driver();

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {
      .sta =
          {
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
              .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
          },
  };

  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
  strncpy((char *)wifi_config.sta.password, password,
          sizeof(wifi_config.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

  return ESP_OK;
}

esp_err_t wifi_start_ap(void) {
  wifi_init_common();
  esp_netif_create_default_wifi_ap();
  wifi_init_driver();

  wifi_config_t wifi_config = {
      .ap =
          {
              .password = WIFI_AP_PASSWORD,
              .max_connection = WIFI_AP_MAX_CONNECTIONS,
              .authmode = WIFI_AUTH_WPA2_PSK,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

  uint8_t mac[6];
  ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));

  int ssid_len = snprintf((char *)wifi_config.ap.ssid,
                          sizeof(wifi_config.ap.ssid), "PEAK-%02X%02X",
                          mac[4], mac[5]);
  wifi_config.ap.ssid_len = (uint8_t)ssid_len;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  return ESP_OK;
}
