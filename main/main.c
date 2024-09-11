
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "sdkconfig.h"

#include "audio_event_iface.h"
#include "audio_element.h"
#include "audio_common.h"
#include "board.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "esp_wifi.h"

#include "peripheral_node/logs.h"
#include "peripheral_node/pipeline.h"

esp_periph_set_handle_t periph_set;
audio_event_iface_handle_t evt;

void initialize_event_listener()
{
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);
}

esp_err_t initialize_audio_board()
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

esp_err_t establish_wifi_connection()
{
    logi(" * Initializing Wi-Fi peripheraals");
    esp_periph_config_t periph_config = DEFAULT_ESP_PERIPH_SET_CONFIG();
    periph_set = esp_periph_set_init(&periph_config);
    periph_wifi_cfg_t wifi_config = {
        .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
        .wifi_config.sta.password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_config);

    logi("* Starting the Wi-Fi peripheral for connection");
    esp_periph_start(periph_set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t stop_wifi_connection()
{
    return esp_periph_set_stop_all(periph_set);
}

void app_main()
{
    logi("[ 1 ] Initializeing the audio board with the audio codec chip");
    initialize_board();

    logi("[ 2 ] Creating audio pipeline for playback");
    pn_pipeline_init();

    logi("[ 4 ] Establishing for Wi-Fi connection (Initializing peripherals)");
    establish_wifi_connection();

    logi("[ 5 ] Stoping the audio pipeline");
    pn_pipeline_destroy();
    pn_pipeline_deinit();

    logi("[ 6 ] Stopping periherals (Wi-FI)");
    stop_wifi_connection();
}
