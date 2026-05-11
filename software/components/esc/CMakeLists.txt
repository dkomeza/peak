
idf_component_register(
    SRCS
        "kt.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_uart
        esp_timer
        freertos
)
