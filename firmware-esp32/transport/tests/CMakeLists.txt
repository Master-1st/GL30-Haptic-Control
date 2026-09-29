cmake_minimum_required(VERSION 3.22)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(gl30_esp_link_tests LANGUAGES C)
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  if(MSVC)
    add_compile_options(/utf-8 /W4)
  else()
    add_compile_options(-Wall -Wextra -Wpedantic)
  endif()
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
if(MSVC)
  target_compile_options(gl30_esp_link_tests PRIVATE /UNDEBUG)
else()
  target_compile_options(gl30_esp_link_tests PRIVATE -UNDEBUG)
endif()

if (NOT MSVC)
  target_link_libraries(gl30_esp_link_tests PRIVATE m)
endif()

add_test(NAME gl30-esp-link-tests COMMAND gl30_esp_link_tests)
set_tests_properties(gl30-esp-link-tests PROPERTIES TIMEOUT 30)
