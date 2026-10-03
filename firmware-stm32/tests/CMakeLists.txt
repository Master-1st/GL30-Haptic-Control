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
  "${STM32_SRC_DIR}/drivers/factory_encoder_as5048a.c"
  "${STM32_SRC_DIR}/drivers/as5048a.c"
  "${STM32_SRC_DIR}/drivers/board_sense.c"
  "${STM32_SRC_DIR}/control/foc.c"
  "${STM32_SRC_DIR}/control/current_zero.c"
  "${STM32_SRC_DIR}/control/control_lease.c"
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

add_executable(control_lease_tests control_lease_tests.c)
target_link_libraries(control_lease_tests PRIVATE gl30_host_test_sources)
target_include_directories(control_lease_tests PRIVATE "${STM32_SRC_DIR}/protocol")
add_test(NAME control-lease-tests COMMAND control_lease_tests)
set_tests_properties(control-lease-tests PROPERTIES TIMEOUT 5)

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

# Product-only hardware branches were not covered by GL30_BUILD_ONLY tests.
add_executable(product_timebase_tests product_timebase_tests.c)
add_executable(product_driver_tests product_driver_tests.c)
target_include_directories(product_driver_tests PRIVATE "${STM32_SRC_DIR}/config")
add_test(NAME product-timebase-tests COMMAND product_timebase_tests)
add_test(NAME product-driver-tests COMMAND product_driver_tests)

add_executable(product_encoder_tests product_encoder_tests.c)
target_include_directories(product_encoder_tests PRIVATE "${STM32_SRC_DIR}/config")
if (NOT MSVC)
  target_link_libraries(product_encoder_tests PRIVATE m)
endif()
add_test(NAME product-encoder-tests COMMAND product_encoder_tests)
set_tests_properties(product-encoder-tests PROPERTIES TIMEOUT 5)

# Compile the exact bounded receive routine with fake DMA and parser endpoints,
# without duplicating its algorithm or faking unrelated product peripherals.
set(PRODUCT_APP "${STM32_SRC_DIR}/app/gl30_app.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PRODUCT_APP}")
file(READ "${PRODUCT_APP}" PRODUCT_APP_SOURCE)
string(FIND "${PRODUCT_APP_SOURCE}" "typedef enum {\n  GL30_POWER_WAIT_LOGIC," POWER_STAGE_START)
string(FIND "${PRODUCT_APP_SOURCE}" "} gl30_power_stage_t;" POWER_STAGE_END)
if(POWER_STAGE_START LESS 0 OR POWER_STAGE_END LESS POWER_STAGE_START)
  message(FATAL_ERROR "Cannot locate product power stage enum for host regressions")
endif()
string(LENGTH "} gl30_power_stage_t;" POWER_STAGE_END_LENGTH)
math(EXPR POWER_STAGE_LENGTH
  "${POWER_STAGE_END} + ${POWER_STAGE_END_LENGTH} - ${POWER_STAGE_START}")
string(SUBSTRING "${PRODUCT_APP_SOURCE}" ${POWER_STAGE_START}
  ${POWER_STAGE_LENGTH} POWER_STAGE_SOURCE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/product_power_stage_under_test.inc"
  "${POWER_STAGE_SOURCE}")

set(POWER_STARTUP_SOURCE "")
foreach(STARTUP_RANGE IN ITEMS HANDLE_FRAME FAULT_PATH ENCODER_GATE SEQUENCER ZERO_HELPER RUN_MONITOR CONTROL_REQUEST PENDING_COMMAND WATCHDOG TIM7_IRQ)
  if(STARTUP_RANGE STREQUAL "HANDLE_FRAME")
    set(STARTUP_START_MARKER "static void handle_parsed_frame(const gl30_frame_t *frame) {")
    set(STARTUP_END_MARKER "static void parse_bytes(const uint8_t *data, size_t length) {")
  elseif(STARTUP_RANGE STREQUAL "FAULT_PATH")
    set(STARTUP_START_MARKER "static void publish_fault_line(bool faulted) {")
    set(STARTUP_END_MARKER "static void configure_dwt_counter(void) {")
  elseif(STARTUP_RANGE STREQUAL "ENCODER_GATE")
    set(STARTUP_START_MARKER "static bool encoder_is_healthy(void) {")
    set(STARTUP_END_MARKER "static bool update_current_zero(")
  elseif(STARTUP_RANGE STREQUAL "SEQUENCER")
    set(STARTUP_START_MARKER "static bool adc_sample_is_fresh(uint64_t now_us) {")
    set(STARTUP_END_MARKER "static void run_monitor(uint64_t now_us) {")
  elseif(STARTUP_RANGE STREQUAL "ZERO_HELPER")
    set(STARTUP_START_MARKER "static bool update_current_zero(const uint16_t raw[3], float offsets[3]) {")
    set(STARTUP_END_MARKER "static void run_slow_sensors(uint64_t now_us) {")
  elseif(STARTUP_RANGE STREQUAL "RUN_MONITOR")
    set(STARTUP_START_MARKER "static void run_monitor(uint64_t now_us) {")
    set(STARTUP_END_MARKER "static void check_watchdog_reset(void) {")
  elseif(STARTUP_RANGE STREQUAL "CONTROL_REQUEST")
    set(STARTUP_START_MARKER "static void process_pending_control_request(void) {")
    set(STARTUP_END_MARKER "static void process_pending_command(void) {")
  elseif(STARTUP_RANGE STREQUAL "PENDING_COMMAND")
    set(STARTUP_START_MARKER "static void process_pending_command(void) {")
    set(STARTUP_END_MARKER "static void send_telemetry(void) {")
  elseif(STARTUP_RANGE STREQUAL "WATCHDOG")
    set(STARTUP_START_MARKER "static void check_watchdog_reset(void) {")
    set(STARTUP_END_MARKER "void gl30_app_init(void) {")
  else()
    set(STARTUP_START_MARKER "void gl30_app_tim7_irq(void) {")
    set(STARTUP_END_MARKER "void gl30_app_dma1_channel1_irq(void) {")
  endif()
  string(FIND "${PRODUCT_APP_SOURCE}" "${STARTUP_START_MARKER}" STARTUP_START)
  string(FIND "${PRODUCT_APP_SOURCE}" "${STARTUP_END_MARKER}" STARTUP_END)
  if(STARTUP_START LESS 0 OR STARTUP_END LESS STARTUP_START)
    message(FATAL_ERROR "Cannot locate product power-startup function range")
  endif()
  math(EXPR STARTUP_LENGTH "${STARTUP_END} - ${STARTUP_START}")
  string(SUBSTRING "${PRODUCT_APP_SOURCE}" ${STARTUP_START}
    ${STARTUP_LENGTH} STARTUP_FUNCTION_SOURCE)
  string(APPEND POWER_STARTUP_SOURCE "${STARTUP_FUNCTION_SOURCE}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/product_power_startup_under_test.inc"
  "${POWER_STARTUP_SOURCE}")

string(FIND "${PRODUCT_APP_SOURCE}" "static void uart_rx_drain(void) {" UART_START)
string(FIND "${PRODUCT_APP_SOURCE}" "static void process_pending_control_request(void) {" UART_END)
if(UART_START LESS 0 OR UART_END LESS UART_START)
  message(FATAL_ERROR "Cannot locate product UART RX routine for host regression")
endif()
math(EXPR UART_LENGTH "${UART_END} - ${UART_START}")
string(SUBSTRING "${PRODUCT_APP_SOURCE}" ${UART_START} ${UART_LENGTH} UART_SOURCE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/product_uart_rx_under_test.inc" "${UART_SOURCE}")
add_executable(product_uart_rx_tests product_uart_rx_tests.c)
target_include_directories(product_uart_rx_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME product-uart-rx-tests COMMAND product_uart_rx_tests)
set_tests_properties(product-timebase-tests product-driver-tests product-uart-rx-tests
  PROPERTIES TIMEOUT 5)

add_executable(current_zero_tests current_zero_tests.c
  "${STM32_SRC_DIR}/control/current_zero.c")
target_include_directories(current_zero_tests PRIVATE
  "${STM32_SRC_DIR}/control" "${STM32_SRC_DIR}/config")
if(NOT MSVC)
  target_link_libraries(current_zero_tests PRIVATE m)
endif()
add_test(NAME current-zero-tests COMMAND current_zero_tests)

# Exercise the actual zero-qualification helper and ADC ISR, not a copied algorithm.
set(ADC_ZERO_SOURCE "")
foreach(FUNCTION_RANGE IN ITEMS ZERO_HELPER ADC_IRQ)
  if(FUNCTION_RANGE STREQUAL "ZERO_HELPER")
    set(START_MARKER "static bool update_current_zero(const uint16_t raw[3], float offsets[3]) {")
    set(END_MARKER "static void run_slow_sensors(uint64_t now_us) {")
  else()
    set(START_MARKER "void gl30_app_adc1_2_irq(void) {")
    set(END_MARKER "void gl30_app_tim1_break_irq(void) {")
  endif()
  string(FIND "${PRODUCT_APP_SOURCE}" "${START_MARKER}" RANGE_START)
  string(FIND "${PRODUCT_APP_SOURCE}" "${END_MARKER}" RANGE_END)
  if(RANGE_START LESS 0 OR RANGE_END LESS RANGE_START)
    message(FATAL_ERROR "Cannot locate product ADC/zero routine for host regression")
  endif()
  math(EXPR RANGE_LENGTH "${RANGE_END} - ${RANGE_START}")
  string(SUBSTRING "${PRODUCT_APP_SOURCE}" ${RANGE_START} ${RANGE_LENGTH} RANGE_SOURCE)
  string(APPEND ADC_ZERO_SOURCE "${RANGE_SOURCE}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/product_adc_zero_under_test.inc" "${ADC_ZERO_SOURCE}")
add_executable(product_adc_zero_tests product_adc_zero_tests.c
  "${STM32_SRC_DIR}/control/current_zero.c")
target_include_directories(product_adc_zero_tests PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${STM32_SRC_DIR}"
  "${STM32_SRC_DIR}/config" "${STM32_SRC_DIR}/control"
  "${STM32_SRC_DIR}/safety" "${STM32_SRC_DIR}/trace"
  "${STM32_SRC_DIR}/protocol" "${STM32_SRC_DIR}/drivers")
if(NOT MSVC)
  target_link_libraries(product_adc_zero_tests PRIVATE m)
endif()
add_test(NAME product-adc-zero-tests COMMAND product_adc_zero_tests)
set_tests_properties(current-zero-tests product-adc-zero-tests PROPERTIES TIMEOUT 5)

# Exercise the actual foreground power preparation and emergency paths.
add_executable(product_power_startup_tests product_power_startup_tests.c
  "${STM32_SRC_DIR}/control/current_zero.c"
  "${STM32_SRC_DIR}/control/control_lease.c"
  "${STM32_SRC_DIR}/protocol/v6_protocol.c"
  "${STM32_SRC_DIR}/safety/safety_supervisor.c")
target_include_directories(product_power_startup_tests PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${STM32_SRC_DIR}"
  "${STM32_SRC_DIR}/config" "${STM32_SRC_DIR}/control"
  "${STM32_SRC_DIR}/drivers" "${STM32_SRC_DIR}/safety"
  "${STM32_SRC_DIR}/trace" "${STM32_SRC_DIR}/protocol")
if(NOT MSVC)
  target_link_libraries(product_power_startup_tests PRIVATE m)
endif()
add_test(NAME product-power-startup-tests COMMAND product_power_startup_tests)
set_tests_properties(product-power-startup-tests PROPERTIES TIMEOUT 5)
