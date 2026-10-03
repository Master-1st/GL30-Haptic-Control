cmake_minimum_required(VERSION 3.20)
project(gl30_kk_preview C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../components")
if(MSVC)
  add_compile_options(/utf-8 /W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
set(KK_OLED "${COMPONENTS}/kk_oled")
set(KK_UI "${COMPONENTS}/kk_ui")
set(APP "${COMPONENTS}/gl30_ui")
set(STM "${CMAKE_CURRENT_LIST_DIR}/../../../firmware-stm32")
add_library(gl30_wire STATIC "${STM}/protocol/v6_protocol.c")
target_include_directories(gl30_wire PUBLIC "${STM}/protocol")
set_target_properties(gl30_wire PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(gl30_model STATIC "${APP}/src/gl30_model.c" "${APP}/src/gl30_preferences.c" "${APP}/src/gl30_gesture.c" "${KK_UI}/src/kk_ui_anim.c")
target_link_libraries(gl30_model PUBLIC gl30_wire)
target_include_directories(gl30_model PUBLIC "${APP}/include" "${COMPONENTS}/gl30_motor/include" PRIVATE "${KK_UI}/include" "${KK_UI}/src" "${KK_OLED}/include")
set_target_properties(gl30_model PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(gl30_preview SHARED
  "${KK_OLED}/graphics/kk_oled.c" "${KK_OLED}/graphics/kk_oled_draw.c"
  "${KK_OLED}/graphics/kk_oled_bitmap.c" "${KK_OLED}/graphics/kk_oled_font.c"
  "${KK_UI}/src/kk_ui.c" "${KK_UI}/src/kk_ui_dialog.c"
  "${KK_UI}/src/kk_ui_draw.c" "${KK_UI}/src/kk_ui_home.c" "${KK_UI}/src/kk_ui_menu.c"
  "${KK_UI}/src/kk_ui_nav.c" "${KK_UI}/src/kk_ui_page.c"
  "${APP}/src/gl30_demo.c" "${APP}/src/gl30_render.c" "${APP}/src/gl30_type.c"
  "${APP}/assets/gl30_type_data.c"
  "${APP}/assets/gl30_font_body.c" "${APP}/assets/gl30_font_title.c"
  "${APP}/assets/gl30_font_digits.c" "${APP}/assets/gl30_font_small.c" "${APP}/assets/gl30_summit.c"
  host_driver.c)
target_include_directories(gl30_preview PUBLIC "${APP}/include" "${KK_OLED}/include"
  PRIVATE "${KK_OLED}/driver" "${KK_OLED}/graphics" "${KK_UI}/include" "${KK_UI}/src")
target_link_libraries(gl30_preview PRIVATE gl30_model)
if(NOT MSVC)
  target_link_libraries(gl30_model PUBLIC m)
  # LEDFont emits concatenated C byte strings larger than ISO's minimum 4095.
  # Both supported GCC and MSVC targets accept their verified 15 KiB length.
  set_source_files_properties("${APP}/assets/gl30_font_body.c" "${APP}/assets/gl30_font_title.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-overlength-strings")
endif()
enable_testing()
add_executable(gl30_model_tests tests/model_tests.c)
target_link_libraries(gl30_model_tests PRIVATE gl30_model)
add_test(NAME model_and_gestures COMMAND gl30_model_tests)
add_executable(gl30_preferences_tests tests/preferences_tests.c)
target_link_libraries(gl30_preferences_tests PRIVATE gl30_model)
add_test(NAME preferences_record_and_runtime_isolation COMMAND gl30_preferences_tests)
add_executable(gl30_render_tests tests/render_tests.c)
target_include_directories(gl30_render_tests PRIVATE "${KK_UI}/include" "${APP}/include" "${KK_OLED}/include" "${KK_OLED}/graphics" "${KK_OLED}/driver")
target_link_libraries(gl30_render_tests PRIVATE gl30_preview gl30_model)
add_test(NAME rgb565_and_ui COMMAND gl30_render_tests)

set(MOTOR "${COMPONENTS}/gl30_motor")
add_executable(gl30_motor_menu_tests tests/motor_menu_tests.c
  "${MOTOR}/src/gl30_menu_session.c" "${CMAKE_CURRENT_LIST_DIR}/../../transport/gl30_esp_link.c"
  "${STM}/protocol/v6_protocol.c" "${STM}/control/foc.c" "${STM}/haptics/haptics.c")
target_include_directories(gl30_motor_menu_tests PRIVATE "${MOTOR}/include"
  "${CMAKE_CURRENT_LIST_DIR}/../../transport" "${STM}/protocol" "${STM}/control"
  "${STM}/config" "${STM}/haptics" "${STM}/safety")
target_link_libraries(gl30_motor_menu_tests PRIVATE gl30_model)
add_test(NAME motor_menu_protocol_and_state COMMAND gl30_motor_menu_tests)

add_executable(gl30_board_dma_tests tests/board_dma_tests.c)
target_include_directories(gl30_board_dma_tests PRIVATE tests/board_fake "${COMPONENTS}/gl30_board/include")
add_test(NAME board_dma_failure_ownership COMMAND gl30_board_dma_tests)

# Execute the actual IDF input/owner source with a deterministic fake SDK.
foreach(target gl30_input_stop_tests gl30_motor_stop_tests)
  string(REPLACE "gl30_" "" test_source "${target}")
  add_executable(${target} "tests/${test_source}.c"
    "${MOTOR}/src/gl30_menu_session.c" "${CMAKE_CURRENT_LIST_DIR}/../../transport/gl30_esp_link.c"
    "${STM}/protocol/v6_protocol.c")
  target_include_directories(${target} PRIVATE tests/stop_fake
    "${CMAKE_CURRENT_LIST_DIR}/../../main" "${CMAKE_CURRENT_LIST_DIR}/../../bsp"
    "${COMPONENTS}/gl30_board/include" "${APP}/include" "${MOTOR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../transport" "${STM}/protocol" "${STM}/control"
    "${STM}/config" "${STM}/haptics" "${STM}/safety"
    "${KK_OLED}/include" "${KK_OLED}/graphics" "${KK_OLED}/driver"
    "${KK_UI}/include" "${KK_UI}/src")
  target_link_libraries(${target} PRIVATE gl30_model)
endforeach()
target_link_libraries(gl30_input_stop_tests PRIVATE gl30_preview)
add_test(NAME input_stop_without_ui_queue COMMAND gl30_input_stop_tests)
add_test(NAME motor_stop_generation_and_owner COMMAND gl30_motor_stop_tests)

add_executable(gl30_settings_store_tests tests/settings_store_tests.c)
target_include_directories(gl30_settings_store_tests PRIVATE tests/settings_fake
  "${CMAKE_CURRENT_LIST_DIR}/../../main" "${COMPONENTS}/gl30_board/include"
  "${MOTOR}/include" "${APP}/include" "${CMAKE_CURRENT_LIST_DIR}/../../transport"
  "${STM}/protocol")
target_link_libraries(gl30_settings_store_tests PRIVATE gl30_model)
add_test(NAME settings_store_reservations_and_failures COMMAND gl30_settings_store_tests)
