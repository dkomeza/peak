set(esc_sources "esc.c")
set(esc_requires freertos esp_driver_gpio esp_driver_uart)

if(CONFIG_PEAK_ESC_BACKEND_KT)
    list(APPEND esc_sources "kt.c")
else()
    list(APPEND esc_sources "cycleiq.c")
    list(APPEND esc_requires connection cycleiq_protocol)
endif()

idf_component_register(
    SRCS ${esc_sources}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${esc_requires}
)
