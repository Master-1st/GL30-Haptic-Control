cmake_minimum_required(VERSION 3.22)

project(gl30_stm32_c_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(STM32_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(gl30_host_test_sources STATIC
  "${STM32_SRC_DIR}/protocol/v6_protocol.c"
  "${STM32_SRC_DIR}/drivers/drv8316.c"
  "${STM32_SRC_DIR}/drivers/drv8316_spi.c"
  "${STM32_SRC_DIR}/drivers/ina228.c"
  "${STM32_SRC_DIR}/drivers/veml7700.c"
  "${STM32_SRC_DIR}/drivers/factory_encoder_pending.c"
  "${STM32_SRC_DIR}/control/foc.c"
  "${STM32_SRC_DIR}/haptics/haptics.c"
  "${STM32_SRC_DIR}/bench/NUCLEO_G474RE_FOC/Core/Src/bench_haptics.c"
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
  "${STM32_SRC_DIR}/bench/NUCLEO_G474RE_FOC/Core/Inc"
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

add_executable(foc_safety_tests foc_safety_tests.c)
target_link_libraries(foc_safety_tests PRIVATE gl30_host_test_sources)
if (NOT MSVC)
  target_link_libraries(foc_safety_tests PRIVATE m)
endif()
target_include_directories(foc_safety_tests PRIVATE
  "${STM32_SRC_DIR}"
  "${STM32_SRC_DIR}/config"
  "${STM32_SRC_DIR}/protocol"
)
add_test(NAME foc-safety-tests COMMAND foc_safety_tests)
set_tests_properties(foc-safety-tests PROPERTIES TIMEOUT 5)

add_executable(bench_safety_tests bench_safety_tests.c
  "${STM32_SRC_DIR}/bench/NUCLEO_G474RE_FOC/Core/Src/bench_safety.c"
  "${STM32_SRC_DIR}/drivers/as5048a.c")
target_include_directories(bench_safety_tests PRIVATE
  "${STM32_SRC_DIR}"
  "${STM32_SRC_DIR}/bench/NUCLEO_G474RE_FOC/Core/Inc"
  "${STM32_SRC_DIR}/drivers")
add_test(NAME bench-safety-tests COMMAND bench_safety_tests)
set_tests_properties(bench-safety-tests PROPERTIES TIMEOUT 5)

# This test translation unit includes the real bench/driver C sources once;
# only MCU registers and LL transport are replaced by test-only fake headers.
add_executable(bench_hw_diagnostic_tests bench_hw_diagnostic_tests.c)
target_include_directories(bench_hw_diagnostic_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/bench_hw_fake"
  "${STM32_SRC_DIR}"
  "${STM32_SRC_DIR}/bench/NUCLEO_G474RE_FOC/Core/Inc"
  "${STM32_SRC_DIR}/drivers")
if (NOT MSVC)
  target_link_libraries(bench_hw_diagnostic_tests PRIVATE m)
endif()
add_test(NAME bench-hw-diagnostic-tests COMMAND bench_hw_diagnostic_tests)
set_tests_properties(bench-hw-diagnostic-tests PROPERTIES TIMEOUT 5)

# Exercise the actual application IRQ/prepare paths with test-only peripherals.
add_executable(bench_app_diagnostic_tests bench_app_diagnostic_tests.c)
target_link_libraries(bench_app_diagnostic_tests PRIVATE gl30_host_test_sources)
target_include_directories(bench_app_diagnostic_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/bench_hw_fake"
  "${STM32_SRC_DIR}"
  "${STM32_SRC_DIR}/bench/NUCLEO_G474RE_FOC/Core/Inc"
  "${STM32_SRC_DIR}/drivers"
  "${STM32_SRC_DIR}/control"
  "${STM32_SRC_DIR}/protocol"
  "${STM32_SRC_DIR}/config")
if (NOT MSVC)
  target_link_libraries(bench_app_diagnostic_tests PRIVATE m)
endif()
add_test(NAME bench-app-diagnostic-tests COMMAND bench_app_diagnostic_tests)
set_tests_properties(bench-app-diagnostic-tests PROPERTIES TIMEOUT 5)

add_executable(haptic_behaviour_tests haptic_behaviour_tests.c)
target_link_libraries(haptic_behaviour_tests PRIVATE gl30_host_test_sources)
target_include_directories(haptic_behaviour_tests PRIVATE
  "${STM32_SRC_DIR}"
  "${STM32_SRC_DIR}/control"
  "${STM32_SRC_DIR}/protocol")
if (NOT MSVC)
  target_link_libraries(haptic_behaviour_tests PRIVATE m)
endif()
add_test(NAME haptic-behaviour-tests COMMAND haptic_behaviour_tests)
set_tests_properties(haptic-behaviour-tests PROPERTIES TIMEOUT 5)

# Actual Q1.31 platform port + FOC, with only ST LL transport simulated.
add_executable(bench_cordic_tests bench_cordic_tests.c)
target_compile_definitions(bench_cordic_tests PRIVATE GL30_FOC_USE_CORDIC=1)
target_include_directories(bench_cordic_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/bench_cordic_fake"
  "${STM32_SRC_DIR}"
  "${STM32_SRC_DIR}/bench/NUCLEO_G474RE_FOC/Core/Inc"
  "${STM32_SRC_DIR}/control" "${STM32_SRC_DIR}/config" "${STM32_SRC_DIR}/protocol")
if (NOT MSVC)
  target_link_libraries(bench_cordic_tests PRIVATE m)
endif()
add_test(NAME bench-cordic-tests COMMAND bench_cordic_tests)
set_tests_properties(bench-cordic-tests PROPERTIES TIMEOUT 5)
