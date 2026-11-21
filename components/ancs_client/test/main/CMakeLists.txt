idf_component_register(
    SRCS "test_ancs_parser.c"
    REQUIRES ancs_client unity
)
project_apply_compile_policy(${COMPONENT_LIB})
