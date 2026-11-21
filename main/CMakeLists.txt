idf_component_register(SRCS "app_main.c" REQUIRES log bt notification_core display_ui ancs_client)
project_apply_compile_policy(${COMPONENT_LIB})
