idf_component_register(SRCS "main.c"
                            "buttons.c"
                            "boot/boot.c"
                       PRIV_REQUIRES connection display esc io vesc wireless
                                      nvs_flash esp_timer
                       INCLUDE_DIRS "." "boot" "utils"
)
