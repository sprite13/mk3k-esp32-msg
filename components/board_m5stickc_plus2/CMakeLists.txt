idf_component_register(
    SRCS "board.c" "board_logic.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_adc esp_driver_gpio esp_driver_ledc esp_timer log
)
project_apply_compile_policy(${COMPONENT_LIB})
