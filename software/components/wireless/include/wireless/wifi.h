#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

/**
 * @brief Initializes the TCP/IP stack and connects to a Wi-Fi network.
 *        Reconnection is handled automatically in the background.
 *
 * @param ssid Target network SSID
 * @param password Target network password
 */
esp_err_t wifi_start(const char *ssid, const char *password);

/**
 * @brief Initializes the TCP/IP stack and starts a Wi-Fi soft AP.
 *        The SSID is generated as PEAK-${last two MAC bytes in hex}.
 */
esp_err_t wifi_start_ap(void);

#endif
