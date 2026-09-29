cmake_minimum_required(VERSION 3.22)
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

# These programs deliberately use assert() to perform both actions and checks.
# Release/LTO must exercise the same tests instead of compiling the calls away.
function(gl30_add_host_test target test_name)
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
  add_test(NAME ${test_name} COMMAND ${target})
  set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
endfunction()

set(KK_OLED "${COMPONENTS}/kk_oled")
set(KK_UI "${COMPONENTS}/kk_ui")
set(APP "${COMPONENTS}/gl30_ui")
add_library(gl30_model STATIC "${APP}/src/gl30_model.c" "${APP}/src/gl30_gesture.c" "${KK_UI}/src/kk_ui_anim.c")
target_include_directories(gl30_model PUBLIC "${APP}/include" PRIVATE "${KK_UI}/include" "${KK_UI}/src" "${KK_OLED}/include")
set_target_properties(gl30_model PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(gl30_preview SHARED
  "${KK_OLED}/graphics/kk_oled.c" "${KK_OLED}/graphics/kk_oled_draw.c"
  "${KK_OLED}/graphics/kk_oled_bitmap.c" "${KK_OLED}/graphics/kk_oled_font.c"
  "${KK_UI}/src/kk_ui.c" "${KK_UI}/src/kk_ui_dialog.c"
  "${KK_UI}/src/kk_ui_draw.c" "${KK_UI}/src/kk_ui_home.c" "${KK_UI}/src/kk_ui_menu.c"
  "${KK_UI}/src/kk_ui_nav.c" "${KK_UI}/src/kk_ui_page.c"
  "${APP}/src/gl30_demo.c" "${APP}/src/gl30_render.c"
  "${APP}/assets/gl30_font_body.c" "${APP}/assets/gl30_font_title.c"
  "${APP}/assets/gl30_font_digits.c" "${APP}/assets/gl30_font_small.c" "${APP}/assets/gl30_summit.c"
  host_driver.c)
target_include_directories(gl30_preview PUBLIC "${APP}/include" "${KK_OLED}/include"
  PRIVATE "${KK_OLED}/driver" "${KK_OLED}/graphics" "${KK_UI}/include" "${KK_UI}/src")
target_link_libraries(gl30_preview PRIVATE gl30_model)
target_compile_definitions(gl30_preview PRIVATE KK_OLED_TEST=1)
if(NOT MSVC)
  target_link_libraries(gl30_model PUBLIC m)
  # LEDFont emits concatenated C byte strings larger than ISO's minimum 4095.
  # GCC and MSVC accept the asset lengths checked by verify_kk_assets.py.
  set_source_files_properties("${APP}/assets/gl30_font_body.c" "${APP}/assets/gl30_font_title.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-overlength-strings")
endif()
enable_testing()
add_executable(gl30_model_tests tests/model_tests.c)
target_link_libraries(gl30_model_tests PRIVATE gl30_model)
gl30_add_host_test(gl30_model_tests model_and_gestures)
add_executable(gl30_render_tests tests/render_tests.c)
target_include_directories(gl30_render_tests PRIVATE "${KK_UI}/include" "${APP}/include" "${KK_OLED}/include" "${KK_OLED}/graphics" "${KK_OLED}/driver")
target_link_libraries(gl30_render_tests PRIVATE gl30_preview gl30_model)
gl30_add_host_test(gl30_render_tests rgb565_and_ui)

add_executable(gl30_display_pipeline_tests tests/display_pipeline_tests.c)
target_compile_definitions(gl30_display_pipeline_tests PRIVATE KK_OLED_TEST=1)
target_include_directories(gl30_display_pipeline_tests PRIVATE
  "${KK_UI}/include" "${APP}/include" "${KK_OLED}/include"
  "${KK_OLED}/graphics" "${KK_OLED}/driver")
target_link_libraries(gl30_display_pipeline_tests PRIVATE gl30_preview gl30_model)
gl30_add_host_test(gl30_display_pipeline_tests display_pipeline_wake_ownership)

set(STM "${CMAKE_CURRENT_LIST_DIR}/../../../firmware-stm32")
set(MOTOR "${COMPONENTS}/gl30_motor")
add_executable(gl30_motor_menu_tests tests/motor_menu_tests.c
  "${MOTOR}/src/gl30_menu_session.c" "${CMAKE_CURRENT_LIST_DIR}/../../transport/gl30_esp_link.c"
  "${STM}/protocol/v6_protocol.c" "${STM}/control/foc.c" "${STM}/haptics/haptics.c")
target_include_directories(gl30_motor_menu_tests PRIVATE "${MOTOR}/include"
  "${CMAKE_CURRENT_LIST_DIR}/../../transport" "${STM}/protocol" "${STM}/control"
  "${STM}/config" "${STM}/haptics" "${STM}/safety")
target_link_libraries(gl30_motor_menu_tests PRIVATE gl30_model)
gl30_add_host_test(gl30_motor_menu_tests motor_menu_protocol_and_state)

add_executable(gl30_board_dma_tests tests/board_dma_tests.c)
target_include_directories(gl30_board_dma_tests PRIVATE tests/board_fake "${COMPONENTS}/gl30_board/include")
gl30_add_host_test(gl30_board_dma_tests board_dma_failure_ownership)
