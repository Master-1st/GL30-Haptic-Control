cmake_minimum_required(VERSION 3.22)

project(gl30_stm32_c_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(STM32_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(gl30_host_test_sources STATIC
  "${STM32_SRC_DIR}/protocol/v6_protocol.c"
  "${STM32_SRC_DIR}/drivers/drv8316.c"
  "${STM32_SRC_DIR}/drivers/ina228.c"
  "${STM32_SRC_DIR}/drivers/veml7700.c"
  "${STM32_SRC_DIR}/drivers/factory_encoder_pending.c"
  "${STM32_SRC_DIR}/control/foc.c"
  "${STM32_SRC_DIR}/haptics/haptics.c"
  "${STM32_SRC_DIR}/safety/safety_supervisor.c"
  "${STM32_SRC_DIR}/trace/trace_buffer.c"
)

target_include_directories(gl30_host_test_sources PRIVATE
  "${STM32_SRC_DIR}"
  "${STM32_SRC_DIR}/drivers"
  "${STM32_SRC_DIR}/control"
  "${STM32_SRC_DIR}/haptics"
  "${STM32_SRC_DIR}/trace"
  "${STM32_SRC_DIR}/protocol"
  "${STM32_SRC_DIR}/safety"
  "${STM32_SRC_DIR}/config"
)

target_compile_definitions(gl30_host_test_sources PRIVATE GL30_BUILD_ONLY=1)

add_executable(gl30_host_tests gl30_host_tests.c)
target_link_libraries(gl30_host_tests PRIVATE gl30_host_test_sources)
if (NOT MSVC)
  target_link_libraries(gl30_host_tests PRIVATE m)
endif()
target_include_directories(gl30_host_tests PRIVATE
  "${STM32_SRC_DIR}"
  "${STM32_SRC_DIR}/drivers"
  "${STM32_SRC_DIR}/control"
  "${STM32_SRC_DIR}/haptics"
  "${STM32_SRC_DIR}/trace"
  "${STM32_SRC_DIR}/protocol"
  "${STM32_SRC_DIR}/safety"
  "${STM32_SRC_DIR}/config"
)
target_compile_definitions(gl30_host_tests PRIVATE
  GL30_BUILD_ONLY=1
  GL30_V1_JSON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/generated/v1.json"
)

enable_testing()
add_test(NAME gl30-host-tests COMMAND gl30_host_tests)
