idf_component_register(
    SRCS "display_smoke.c"
    REQUIRES display_ui
)
project_apply_compile_policy(${COMPONENT_LIB})
