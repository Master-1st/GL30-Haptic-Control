cmake_minimum_required(VERSION 3.22)

project(gl30_ui LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8)
endif()

set(GL30_UI_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(GL30_UI_BUILD_HOST_PREVIEW OFF CACHE BOOL "Build LVGL-backed host preview/view target")
set(GL30_UI_LVGL_SOURCE_DIR "" CACHE PATH "Path to LVGL source tree (preferred) for host build")
set(GL30_UI_FETCH_LVGL OFF CACHE BOOL "Fetch LVGL v9.2.0 from GitHub when source path is not provided")

set(GL30_UI_SOURCE_DIR "${GL30_UI_ROOT_DIR}/src")
set(GL30_UI_INCLUDE_DIR "${GL30_UI_ROOT_DIR}/include")

add_library(gl30_ui_anim_model
  "${GL30_UI_SOURCE_DIR}/gl30_ui_anim_model.c"
  "${GL30_UI_SOURCE_DIR}/gl30_ui_app.c"
)
target_include_directories(gl30_ui_anim_model PUBLIC "${GL30_UI_INCLUDE_DIR}")
if(NOT MSVC)
  target_link_libraries(gl30_ui_anim_model PUBLIC m)
endif()
include(CTest)
add_subdirectory("${GL30_UI_ROOT_DIR}/../transport" "${CMAKE_CURRENT_BINARY_DIR}/transport")
add_library(gl30_ui_controller "${GL30_UI_SOURCE_DIR}/gl30_ui_controller.c")
target_link_libraries(gl30_ui_controller PUBLIC gl30_ui_anim_model gl30_esp_link)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

add_executable(gl30_ui_host_model_demo
  "${GL30_UI_SOURCE_DIR}/gl30_ui_host_model_demo.c"
)
target_link_libraries(gl30_ui_host_model_demo PRIVATE gl30_ui_anim_model)
target_include_directories(gl30_ui_host_model_demo PRIVATE "${GL30_UI_INCLUDE_DIR}")

if (GL30_UI_BUILD_HOST_PREVIEW)
  set(_LVGL_SOURCE_DIR "${GL30_UI_LVGL_SOURCE_DIR}")
  set(LVGL_TARGET_TAG aa7446344c6ec7631112ef031983ef24077e24d5)
  if (_LVGL_SOURCE_DIR STREQUAL "")
    if (GL30_UI_FETCH_LVGL)
      include(FetchContent)
      message(STATUS "LVGL source not provided, fetching lvgl v9.2.0 (${LVGL_TARGET_TAG})")
      FetchContent_Declare(
        lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG ${LVGL_TARGET_TAG}
        GIT_SHALLOW FALSE
      )
      FetchContent_GetProperties(lvgl)
      if (NOT lvgl_POPULATED)
        FetchContent_Populate(lvgl)
      endif()
      set(_LVGL_SOURCE_DIR "${lvgl_SOURCE_DIR}")
    else()
      message(FATAL_ERROR
        "GL30_UI_BUILD_HOST_PREVIEW=ON requires LVGL source path (GL30_UI_LVGL_SOURCE_DIR) "
        "or GL30_UI_FETCH_LVGL=ON."
      )
    endif()
  endif()

  set(LV_CONF_PATH "${GL30_UI_ROOT_DIR}/lvgl_host_conf.h"
      CACHE FILEPATH "LVGL configuration" FORCE)
  set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
  set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
  set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
  add_subdirectory("${_LVGL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
  # LVGL 9.2's Windows CMake attaches DLL attributes even to its static target.
  # Static consumers must refer to data symbols directly, not through __imp_.
  if(WIN32 AND NOT BUILD_SHARED_LIBS)
    foreach(property COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
      get_target_property(definitions lvgl ${property})
      if(definitions)
        list(FILTER definitions EXCLUDE REGEX "^LV_ATTRIBUTE_EXTERN_DATA=")
        set_property(TARGET lvgl PROPERTY ${property} "${definitions}")
      endif()
    endforeach()
  endif()

  add_library(gl30_ui_anim_view
    "${GL30_UI_SOURCE_DIR}/gl30_ui_anim_view.c"
  )
  target_include_directories(gl30_ui_anim_view PRIVATE "${GL30_UI_INCLUDE_DIR}")
  if (TARGET lvgl)
    target_link_libraries(gl30_ui_anim_view PRIVATE lvgl gl30_ui_anim_model)
  else()
    message(FATAL_ERROR "LVGL CMake target not found after add_subdirectory")
  endif()

  add_executable(gl30_ui_host_demo
    "${GL30_UI_SOURCE_DIR}/gl30_ui_host_demo.c"
  )
  target_include_directories(gl30_ui_host_demo PRIVATE "${GL30_UI_INCLUDE_DIR}")
  target_link_libraries(gl30_ui_host_demo PRIVATE gl30_ui_anim_view gl30_ui_anim_model lvgl)
endif()
