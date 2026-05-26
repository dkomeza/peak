
idf_component_register(
    SRCS
        "controller.c"
        "kt.c"
        "peak.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        cycleiq_protocol
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_uart
        esp_timer
        freertos
        connection
)
