idf_component_register(SRCS "main.c" "buttons.c" "boot/boot.c"
                       PRIV_REQUIRES ltr329 t117 button esp_wifi nvs_flash esp_timer
                                     esp_lcd
                       INCLUDE_DIRS "boot" "." "utils")
