cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32")
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_LIST_DIR}/../../../board_m5stickc_plus2"
    "${CMAKE_CURRENT_LIST_DIR}/../../../notification_core"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/project_compile_policy.cmake)

set(REQUIRED_IDF_VERSION "6.1.1")
set(ACTUAL_IDF_VERSION
    "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")
if(NOT "${ACTUAL_IDF_VERSION}" STREQUAL "${REQUIRED_IDF_VERSION}")
    message(FATAL_ERROR
        "ESP-IDF ${REQUIRED_IDF_VERSION} is required; found ${ACTUAL_IDF_VERSION}")
endif()

idf_build_set_property(MINIMAL_BUILD ON)
project(display_smoke)
