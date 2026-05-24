idf_component_register(
    SRCS
        "src/loom.c"
        "src/loom_command.c"
        "src/loom_raster.c"
        "src/loom_backend_esp32p4.c"
        "src/loom_font_noto_sans_16.c"
        "src/loom_font_noto_sans_32.c"
        "src/loom_font_noto_sans_digits_96.c"
        "src/loom_font_noto_sans_digits_144.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd
    PRIV_REQUIRES esp_driver_ppa heap esp_hw_support esp_timer
)
