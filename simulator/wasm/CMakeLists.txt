cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(m5_ancs_simulator_api C)
    include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/project_compile_policy.cmake)
endif()

set(SIMULATOR_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

if(NOT TARGET notification_core)
    add_library(notification_core
        ${SIMULATOR_ROOT}/components/notification_core/notification_queue.c
        ${SIMULATOR_ROOT}/components/notification_core/notification_normalizer.c
    )
    target_include_directories(notification_core PUBLIC
        ${SIMULATOR_ROOT}/components/notification_core/include
    )
    project_apply_compile_policy(notification_core)
endif()

if(NOT TARGET app_core)
    add_library(app_core
        ${SIMULATOR_ROOT}/components/app_core/app_state.c
        ${SIMULATOR_ROOT}/components/app_core/app_screen.c
    )
    target_include_directories(app_core PUBLIC
        ${SIMULATOR_ROOT}/components/app_core/include
    )
    target_link_libraries(app_core PUBLIC notification_core)
    project_apply_compile_policy(app_core)
endif()

add_library(simulator_api simulator_api.c)
target_include_directories(simulator_api PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(simulator_api PUBLIC app_core)
project_apply_compile_policy(simulator_api)
