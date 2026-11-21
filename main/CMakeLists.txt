idf_component_register(SRCS "app_main.c" REQUIRES log bt notification_core display_ui)
project_apply_compile_policy(${COMPONENT_LIB})
