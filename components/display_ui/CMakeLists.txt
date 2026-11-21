idf_component_register(
    SRCS "display_ui.c" "display_layout.c" "font_12x20_cyrillic.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES board_m5stickc_plus2 esp_lcd notification_core
)
project_apply_compile_policy(${COMPONENT_LIB})
