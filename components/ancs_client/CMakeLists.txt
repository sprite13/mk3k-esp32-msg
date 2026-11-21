idf_component_register(
    SRCS "ancs_client.c" "ancs_parser.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES bt notification_core nvs_flash esp_timer
)
project_apply_compile_policy(${COMPONENT_LIB})
