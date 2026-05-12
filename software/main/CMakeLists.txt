idf_component_register(SRCS "main.c"
                            "buttons.c"
                            "boot/boot.c"
                            "display/display.c"
                            "ui/ui.c"
                            "ui/style.c"
                            "ui/dashboard.c"
                       PRIV_REQUIRES io vesc connection wireless esc 
                                      nvs_flash esp_timer esp_lcd
                                      esp_lcd_st7701 lvgl
                       INCLUDE_DIRS "." "boot" "utils"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE)
