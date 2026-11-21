idf_component_register(
    SRCS "test_ancs_parser.c"
    REQUIRES ancs_client unity
    PRIV_INCLUDE_DIRS "../.."
)
project_apply_compile_policy(${COMPONENT_LIB})
