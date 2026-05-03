idf_component_register(
  SRCS
    "crc.c"
    "packet.c"
    "vesc_bridge.c"
  INCLUDE_DIRS
    "include"
  PRIV_INCLUDE_DIRS
    "include/vesc"
  PRIV_REQUIRES
    esp_timer
    connection
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE NO_STM32)
