idf_component_register(
    SRCS
        "display.c"
        "ui/home.c"
        "../../main/display/display_port.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        esp_lcd
        esp_lcd_st7701
        esp_timer
        freertos
        lvgl
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
