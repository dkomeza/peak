idf_component_register(SRCS "main.c"
                            "display_event_adapter.c"
                            "buttons.c"
                            "power.c"
                            "boot/boot.c"
                       PRIV_REQUIRES display esc io nvs_flash esp_timer driver
                                      esp_hw_support
                       INCLUDE_DIRS "." "boot" "utils"
)
