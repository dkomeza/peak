idf_component_register(SRCS "main.c"
                            "buttons.c"
                            "boot/boot.c"
                            "display/display_port.c"
                            "display/components/background.c"
                            "display/components/card.c"
                            "display/assets/blured_circle.c"
                       PRIV_REQUIRES io vesc connection wireless esc
                                      nvs_flash esp_timer esp_lcd esp_lcd_st7701 
                                      lvgl
                       INCLUDE_DIRS "." "boot" "utils"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
