idf_component_register(
  SRCS
    "udp_bridge.c"
    "wifi.c"
  INCLUDE_DIRS
    "include"
  PRIV_INCLUDE_DIRS
    "include/wireless"
  REQUIRES
    vesc

  PRIV_REQUIRES
    esp_wifi
    esp_netif
    esp_event
    lwip
) 
