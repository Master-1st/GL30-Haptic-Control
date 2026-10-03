if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.22)
  project(gl30_esp_link_tests LANGUAGES C)
endif()

include(CTest)

set(STM32_PROTOCOL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../firmware-stm32/protocol")
set(STM32_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../firmware-stm32/config")

if(NOT TARGET gl30_esp_link)
  add_library(gl30_esp_link
    ../gl30_esp_link.c
    "${STM32_PROTOCOL_DIR}/v6_protocol.c"
  )
  target_include_directories(gl30_esp_link PUBLIC
    ../
    "${STM32_PROTOCOL_DIR}"
    "${STM32_CONFIG_DIR}"
  )
endif()

add_executable(gl30_esp_link_tests
  test_gl30_esp_link.c
)
target_link_libraries(gl30_esp_link_tests PRIVATE gl30_esp_link)

if (NOT MSVC)
  target_link_libraries(gl30_esp_link_tests PRIVATE m)
endif()

add_test(NAME gl30-esp-link-tests COMMAND gl30_esp_link_tests)
