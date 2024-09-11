
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

#include "http_stream.h"

#include "peripheral_node/logs.h"
#include "peripheral_node/pipeline.h"

audio_element_handle_t http_stream_reader, i2s_stream_writer, mp3_decoder;

esp_periph_set_handle_t periph_set;
audio_event_iface_handle_t event;

void initialize_event_listener()
{
    audio_event_iface_cfg_t event_config = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    event = audio_event_iface_init(&event_config);
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

    ESP_ERROR_CHECK(esp_netif_init());

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

int event_handle_for_http_stream(http_stream_event_msg_t *message)
{
    if (message->event_id == HTTP_STREAM_RESOLVE_ALL_TRACKS)
    {
        return ESP_OK;
    }

    if (message->event_id == HTTP_STREAM_FINISH_TRACK)
    {
        return http_stream_next_track(message->el);
    }
    if (message->event_id == HTTP_STREAM_FINISH_PLAYLIST)
    {
        return http_stream_fetch_again(message->el);
    }
    return ESP_OK;
}

void app_main()
{
    logi("[ 1 ] Initializeing the audio board with the audio codec chip");
    initialize_audio_board();

    logi("[ 2 ] Creating audio pipeline for playback");
    pn_pipeline_init();

    logi("[ 3 ] Creating reqired audio elements for pipeline");
    logi(" * Initializing an HTTP stream reader to read the incomig HTTP audio");
    http_stream_cfg_t http_config = HTTP_STREAM_CFG_DEFAULT();
    http_config.event_handle = event_handle_for_http_stream;
    http_config.type = AUDIO_STREAM_READER;
    http_config.enable_playlist_parser = true;
    http_stream_reader = http_stream_init(&http_config);

    logi("[ 4 ] Initialize event listener");
    initialize_event_listener();

    logi("[ 5 ] Establishing for Wi-Fi connection (Initializing peripherals)");
    establish_wifi_connection();

    while (1)
    {
        audio_event_iface_msg_t message;
        esp_err_t event_result = audio_event_iface_listen(event, &message, portMAX_DELAY);

        if (event_result != ESP_OK)
        {
            loge("[ ! ] Event interface error : %d", event_result);
            continue;
        }

        logi("*** Here %d", message.source_type);
    }

    logi("[ 6 ] Stoping the audio pipeline");
    pn_pipeline_destroy();
    pn_pipeline_deinit();

    logi("[ 7 ] Stopping periherals (Wi-FI)");
    stop_wifi_connection();
}
