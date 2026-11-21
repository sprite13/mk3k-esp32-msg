cmake_minimum_required(VERSION 3.16)
project(m5_ancs_host_tests C)
enable_testing()

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/project_compile_policy.cmake)

set(CORE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../components/notification_core)
add_library(notification_core
    ${CORE_DIR}/notification_queue.c
    ${CORE_DIR}/notification_normalizer.c
)
target_include_directories(notification_core PUBLIC ${CORE_DIR}/include)
project_apply_compile_policy(notification_core)

add_executable(test_notification_queue test_notification_queue.c)
target_link_libraries(test_notification_queue PRIVATE notification_core)
project_apply_compile_policy(test_notification_queue)
add_test(NAME notification_queue COMMAND test_notification_queue)

add_executable(test_notification_normalizer test_notification_normalizer.c)
target_link_libraries(test_notification_normalizer PRIVATE notification_core)
project_apply_compile_policy(test_notification_normalizer)
add_test(NAME notification_normalizer COMMAND test_notification_normalizer)

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
