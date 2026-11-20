idf_component_register(
    SRCS "board_smoke.c"
    REQUIRES board_m5stickc_plus2 log
)
project_apply_compile_policy(${COMPONENT_LIB})
