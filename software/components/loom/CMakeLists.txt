idf_component_register(
    SRCS
        "src/loom.c"
        "src/loom_command.c"
        "src/loom_raster.c"
        "src/loom_backend_esp32p4.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd
    PRIV_REQUIRES heap
)
