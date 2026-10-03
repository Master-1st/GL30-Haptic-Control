set(STM "${CMAKE_CURRENT_LIST_DIR}/../../../firmware-stm32")
set(LINK "${CMAKE_CURRENT_LIST_DIR}/../../transport")
idf_component_register(SRCS "src/gl30_motor.c" "src/gl30_menu_session.c"
    "${LINK}/gl30_esp_link.c" "${STM}/protocol/v6_protocol.c"
    INCLUDE_DIRS "include" "${LINK}" "${STM}/protocol"
    PRIV_INCLUDE_DIRS "${STM}/control" "${STM}/config" "${STM}/safety"
    REQUIRES esp_driver_uart esp_timer)
