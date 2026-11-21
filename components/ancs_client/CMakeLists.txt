idf_component_register(
    SRCS "ancs_client.c" "ancs_parser.c" "ancs_request_queue.c"
    INCLUDE_DIRS "include" "internal_include"
    REQUIRES bt notification_core nvs_flash esp_timer
)
project_apply_compile_policy(${COMPONENT_LIB})
