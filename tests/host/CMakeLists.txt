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
