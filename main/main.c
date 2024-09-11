
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "sdkconfig.h"

#include "audio_event_iface.h"
#include "audio_element.h"
#include "audio_common.h"
#include "board.h"
#include "peripheral_node/logs.h"
#include "peripheral_node/pipeline.h"

esp_err_t initialize_board()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
    return ESP_OK;
}

void app_main()
{
    logi("[ 1 ] Initializeing the audio board with the audio codec chip");
    initialize_board();

    logi("[ 2 ] Creating audio pipeline for playback");
    pn_pipeline_init();

    logi("[ 3 ] Stoping the audio pipeline");
    pn_pipeline_destroy();
    pn_pipeline_deinit();
}
