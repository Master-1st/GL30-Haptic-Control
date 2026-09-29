cmake_minimum_required(VERSION 3.16)

# Keep the ESP32 product snapshot independent from repository-wide STM32 bench tags.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" PROJECT_VER LIMIT_COUNT 1)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gl30_haptic_control)
