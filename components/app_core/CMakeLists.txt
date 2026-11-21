idf_component_register(
    SRCS "app_state.c" "app_screen.c"
    INCLUDE_DIRS "include"
    REQUIRES notification_core
)
project_apply_compile_policy(${COMPONENT_LIB})
