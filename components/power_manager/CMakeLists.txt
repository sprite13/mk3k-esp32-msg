idf_component_register(
    SRCS "power_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_pm
)
project_apply_compile_policy(${COMPONENT_LIB})
