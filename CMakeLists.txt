cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(REQUIRED_IDF_VERSION "6.1.1")
set(ACTUAL_IDF_VERSION
    "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")
if(NOT "${ACTUAL_IDF_VERSION}" STREQUAL "${REQUIRED_IDF_VERSION}")
    message(FATAL_ERROR
        "ESP-IDF ${REQUIRED_IDF_VERSION} is required; found ${ACTUAL_IDF_VERSION}")
endif()

idf_build_set_property(MINIMAL_BUILD ON)
project(m5stickc_telegram_ancs)
