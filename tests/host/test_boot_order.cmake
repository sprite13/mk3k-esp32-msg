file(READ "${APP_MAIN}" APP_MAIN_SOURCE)

string(FIND "${APP_MAIN_SOURCE}" "void app_main(void)" APP_MAIN_POSITION)
string(FIND "${APP_MAIN_SOURCE}" "board_power_hold_init();" POWER_HOLD_POSITION)
string(FIND "${APP_MAIN_SOURCE}" "ESP_LOGI(TAG, \"m5stickc-telegram-ancs boot\")" LOG_POSITION)
string(FIND "${APP_MAIN_SOURCE}" "nvs_flash_init();" NVS_POSITION)

if(APP_MAIN_POSITION LESS 0 OR POWER_HOLD_POSITION LESS 0 OR
   LOG_POSITION LESS 0 OR NVS_POSITION LESS 0)
    message(FATAL_ERROR "boot sequence operation missing")
endif()
if(NOT (APP_MAIN_POSITION LESS POWER_HOLD_POSITION AND
        POWER_HOLD_POSITION LESS LOG_POSITION AND
        LOG_POSITION LESS NVS_POSITION))
    message(FATAL_ERROR "power hold must be the first app_main operation")
endif()
