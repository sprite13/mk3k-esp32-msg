idf_component_register(
    SRCS "notification_queue.c" "notification_normalizer.c"
    INCLUDE_DIRS "include"
)
project_apply_compile_policy(${COMPONENT_LIB})
