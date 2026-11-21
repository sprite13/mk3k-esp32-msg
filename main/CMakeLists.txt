idf_component_register(
    SRCS "app_main.c" "app_notification_admission.c" "app_state.c"
    INCLUDE_DIRS "."
    REQUIRES ancs_client board_m5stickc_plus2 display_ui esp_timer freertos log
             notification_core nvs_flash
)
project_apply_compile_policy(${COMPONENT_LIB})
