if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.22)
  project(gl30_ui_anim_model_tests LANGUAGES C)
endif()

include(CTest)

add_executable(gl30_ui_anim_model_tests
  test_gl30_ui_anim_model.c
)
add_executable(gl30_ui_app_tests
  test_gl30_ui_app.c
)
add_executable(gl30_ui_controller_tests
  test_gl30_ui_controller.c
)

if (NOT TARGET gl30_ui_anim_model)
  add_library(gl30_ui_anim_model
    ../src/gl30_ui_anim_model.c
    ../src/gl30_ui_app.c
  )
  target_include_directories(gl30_ui_anim_model PUBLIC ../include)
endif()
if (NOT TARGET gl30_esp_link)
  set(STM32_PROTOCOL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../firmware-stm32/protocol")
  set(STM32_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../firmware-stm32/config")
  add_library(gl30_esp_link
    ../../transport/gl30_esp_link.c
    "${STM32_PROTOCOL_DIR}/v6_protocol.c"
  )
  target_include_directories(gl30_esp_link PUBLIC
    ../../transport
    "${STM32_PROTOCOL_DIR}"
    "${STM32_CONFIG_DIR}"
  )
endif()
if (NOT TARGET gl30_ui_controller)
  add_library(gl30_ui_controller
    ../src/gl30_ui_controller.c
  )
  target_include_directories(gl30_ui_controller PUBLIC
    ../include
    ../../transport
  )
  target_link_libraries(gl30_ui_controller PUBLIC gl30_ui_anim_model)
  target_link_libraries(gl30_ui_controller PUBLIC gl30_esp_link)
endif()

target_link_libraries(gl30_ui_anim_model_tests PRIVATE gl30_ui_anim_model)
target_link_libraries(gl30_ui_app_tests PRIVATE gl30_ui_anim_model)
target_link_libraries(gl30_ui_controller_tests PRIVATE gl30_ui_controller)

if (NOT MSVC)
  # The model uses libm; propagate it after dependent static archives too.
  target_link_libraries(gl30_ui_anim_model PUBLIC m)
endif()

add_test(NAME gl30_ui_anim_model_tests COMMAND gl30_ui_anim_model_tests)
add_test(NAME gl30_ui_app_tests COMMAND gl30_ui_app_tests)
add_test(NAME gl30_ui_controller_tests COMMAND gl30_ui_controller_tests)
