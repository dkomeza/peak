idf_component_register(SRCS "main.c"
                            "buttons.c"
                            "boot/boot.c"
                            "wireless/wifi.c"     # Updated
                            "wireless/udp_bridge.c"
                            "wireless/ble.c"      # Added
                            "wireless/ble_bridge.c"
                            "can/can.c"               # Updated
                            "vesc_bridge/vesc_bridge.c"
                            "../vesc/packet.c"
                            "../vesc/crc.c"
                       PRIV_REQUIRES ltr329 t117 button esp_wifi nvs_flash esp_timer
                                     esp_lcd esp_driver_twai espressif__esp_hosted
                                     bt lwip esp_netif esp_event  # Added new ESP-IDF modules
                       INCLUDE_DIRS "boot" "." "utils" "can" "vesc_bridge"
                                    "../vesc" "wireless")             # Added wireless dir

target_compile_definitions(${COMPONENT_LIB} PRIVATE NO_STM32)
