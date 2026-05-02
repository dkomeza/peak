idf_component_register(SRCS "main.c" "buttons.c" "boot/boot.c" 
                            "wireless/wifi.c" "wireless/udp_bridge.c"
                            "can/can.c"
                            "vesc_bridge/vesc_bridge.c"
                            "../vesc/packet.c"
                            "../vesc/crc.c"
                       PRIV_REQUIRES ltr329 t117 button esp_wifi nvs_flash esp_timer
                                     esp_lcd esp_driver_twai
                       INCLUDE_DIRS "boot" "." "utils" "can" "vesc_bridge"
                                    "../vesc")

target_compile_definitions(${COMPONENT_LIB} PRIVATE NO_STM32)
