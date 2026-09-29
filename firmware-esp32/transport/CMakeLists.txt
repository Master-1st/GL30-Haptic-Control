cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(gl30_esp_transport LANGUAGES C)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8 /W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(GL30_STM32_PROTOCOL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../firmware-stm32/protocol")
set(GL30_STM32_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/../../firmware-stm32/config")

add_library(gl30_esp_link
  gl30_esp_link.c
  "${GL30_STM32_PROTOCOL_DIR}/v6_protocol.c"
)
target_include_directories(gl30_esp_link
  PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
    "${GL30_STM32_PROTOCOL_DIR}"
    "${GL30_STM32_CONFIG_DIR}"
)

if(NOT MSVC)
  target_link_libraries(gl30_esp_link PRIVATE m)
endif()

add_executable(gl30_esp_replay_cli gl30_esp_replay_cli.c)
target_link_libraries(gl30_esp_replay_cli PRIVATE gl30_esp_link)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
