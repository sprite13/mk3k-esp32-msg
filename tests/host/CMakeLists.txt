cmake_minimum_required(VERSION 3.16)
project(m5_ancs_host_tests C)
enable_testing()

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/project_compile_policy.cmake)

set(CORE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../components/notification_core)
add_library(notification_core
    ${CORE_DIR}/notification_queue.c
    ${CORE_DIR}/notification_normalizer.c
    ${CMAKE_CURRENT_LIST_DIR}/../../main/app_state.c
)
target_include_directories(notification_core PUBLIC
    ${CORE_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../../main
)
project_apply_compile_policy(notification_core)

add_executable(test_notification_queue test_notification_queue.c)
target_link_libraries(test_notification_queue PRIVATE notification_core)
project_apply_compile_policy(test_notification_queue)
add_test(NAME notification_queue COMMAND test_notification_queue)

add_executable(test_notification_normalizer test_notification_normalizer.c)
target_link_libraries(test_notification_normalizer PRIVATE notification_core)
project_apply_compile_policy(test_notification_normalizer)
add_test(NAME notification_normalizer COMMAND test_notification_normalizer)

add_executable(test_app_state test_app_state.c)
target_link_libraries(test_app_state PRIVATE notification_core)
project_apply_compile_policy(test_app_state)
add_test(NAME app_state COMMAND test_app_state)

set(BOARD_DIR ${CMAKE_CURRENT_LIST_DIR}/../../components/board_m5stickc_plus2)
add_executable(test_board_logic
    test_board_logic.c
    ${BOARD_DIR}/board_logic.c
)
target_include_directories(test_board_logic PRIVATE ${BOARD_DIR})
project_apply_compile_policy(test_board_logic)
add_test(NAME board_logic COMMAND test_board_logic)

set(DISPLAY_DIR ${CMAKE_CURRENT_LIST_DIR}/../../components/display_ui)
add_executable(test_display_layout
    test_display_layout.c
    ${DISPLAY_DIR}/display_layout.c
    ${DISPLAY_DIR}/font_12x20_cyrillic.c
)
target_include_directories(test_display_layout PRIVATE
    ${DISPLAY_DIR}
    ${CORE_DIR}/include
)
project_apply_compile_policy(test_display_layout)
add_test(NAME display_layout COMMAND test_display_layout)

add_executable(test_display_lifecycle
    test_display_lifecycle.c
    ${DISPLAY_DIR}/display_lifecycle.c
)
target_include_directories(test_display_lifecycle PRIVATE ${DISPLAY_DIR})
project_apply_compile_policy(test_display_lifecycle)
add_test(NAME display_lifecycle COMMAND test_display_lifecycle)

set(ANCS_DIR ${CMAKE_CURRENT_LIST_DIR}/../../components/ancs_client)
add_executable(test_ancs_parser
    test_ancs_parser.c
    ${ANCS_DIR}/ancs_parser.c
    ${ANCS_DIR}/ancs_request_queue.c
)
target_include_directories(test_ancs_parser PRIVATE
    ${ANCS_DIR}/internal_include
    ${ANCS_DIR}/include
    ${CORE_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/stubs
)
project_apply_compile_policy(test_ancs_parser)
add_test(NAME ancs_parser COMMAND test_ancs_parser)

add_executable(test_ancs_lifecycle
    test_ancs_lifecycle.c
    ${ANCS_DIR}/ancs_lifecycle.c
    ${ANCS_DIR}/ancs_request_queue.c
)
target_include_directories(test_ancs_lifecycle PRIVATE
    ${ANCS_DIR}/internal_include
)
project_apply_compile_policy(test_ancs_lifecycle)
add_test(NAME ancs_lifecycle COMMAND test_ancs_lifecycle)
