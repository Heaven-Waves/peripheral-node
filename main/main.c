
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
#include "i2s_stream.h"
#include "opus_decoder.h"
#include "mp3_decoder.h"

#include "peripheral_node/logs.h"
#include "peripheral_node/pipeline.h"

#define STREAM_URI "http://149.13.0.80/nrj128"

audio_element_handle_t http_stream_reader, i2s_stream_writer, mp3_decoder;
const char *http_stream_tag = "http";
const char *mp3_decoder_tag = "mp3";
const char *i2s_stream_tag = "i2s";

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
    logi("[-*-] Initializing Wi-Fi peripheraals");
    esp_periph_config_t periph_config = DEFAULT_ESP_PERIPH_SET_CONFIG();
    periph_set = esp_periph_set_init(&periph_config);
    periph_wifi_cfg_t wifi_config = {
        .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
        .wifi_config.sta.password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_config);

    logi("[-*-] Starting the Wi-Fi peripheral for connection");
    esp_periph_start(periph_set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t stop_wifi_connection()
{
    esp_periph_set_stop_all(periph_set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(periph_set), event);
    return ESP_OK;
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

// void initialize_http_stream_reader() {}
// void initialize_mp3_dedcoder() {}
// void initialize_i2s_stream_writer() {}

void app_main()
{
    logi("[ 1 ] Initializeing the audio board with the audio codec chip");
    initialize_audio_board();

    logi("[ 2 ] Creating audio pipeline for playback");
    pn_pipeline_init();

    logi("[ 3 ] Creating reqired audio elements for pipeline");
    logi("[-*-] Initializing an HTTP stream reader to read the incomig HTTP audio");
    http_stream_cfg_t http_config = HTTP_STREAM_CFG_DEFAULT();
    http_config.event_handle = event_handle_for_http_stream;
    http_config.type = AUDIO_STREAM_READER;
    http_config.enable_playlist_parser = true;
    http_stream_reader = http_stream_init(&http_config);

    logi("[-*-] Initializing an MP3 decoder to decode the incoming HTTP stream encoded with MP3");
    mp3_decoder_cfg_t mp3_config = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_config);

    logi("[-*-] Initializing an I2S stream to write data to codec chip");
    i2s_stream_cfg_t i2s_stream_config = I2S_STREAM_CFG_DEFAULT();
    i2s_stream_config.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_stream_config);

    logi("[ 4 ] Registering all these elements to the dedicated audio pipeline");
    pn_pipeline_register(http_stream_reader, http_stream_tag);
    pn_pipeline_register(mp3_decoder, mp3_decoder_tag);
    pn_pipeline_register(i2s_stream_writer, i2s_stream_tag);

    logi("[ 5 ] Linking the elements in proper order: http_stream-->mp3_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {http_stream_tag, mp3_decoder_tag, i2s_stream_tag};
    pn_pipeline_link(&link_tag[0], 3);

    logi("[ 6 ] Setting up streaming URI for the HTTP stream reader");
    audio_element_set_uri(http_stream_reader, STREAM_URI);

    logi("[ 7 ] Initialize event listener");
    initialize_event_listener();
    logi(" * Catching all events from the elements inside the pipeline");
    pn_pipeline_set_listener(event);

    logi("[ 8 ] Establishing for Wi-Fi connection (Initializing peripherals)");
    establish_wifi_connection();

    logi("[ 9 ] Starting the audio pipeline");
    pn_pipeline_run();
    while (1)
    {
        audio_event_iface_msg_t message;
        esp_err_t event_result = audio_event_iface_listen(event, &message, portMAX_DELAY);

        if (event_result != ESP_OK)
        {
            loge("[ ! ] Event interface error : %d", event_result);
            continue;
        }

        /* If the incomming message was originated from the MP3 decoder */
        if (message.source == (void *)mp3_decoder &&
            message.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
            message.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO)
        {
            audio_element_info_t sound_information = {0};
            audio_element_getinfo(mp3_decoder, &sound_information);

            logi("[-*-] Receiving information from `mp3_decoder`:");
            logi("[-*-] - sample_rates = %d", sound_information.sample_rates);
            logi("[-*-] - bits = %d", sound_information.bits);
            logi("[-*-] - channels = %d", sound_information.channels);

            /* Setup the internal I2S clock according to decoded MP3 stream */
            i2s_stream_set_clk(
                i2s_stream_writer,
                sound_information.sample_rates,
                sound_information.bits,
                sound_information.channels);

            continue;
        }

        /* Restart the stream if the http_stream_reader receives stop event (caused by reading errors) */
        if (message.source == (void *)http_stream_reader &&
            message.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
            message.cmd == AEL_MSG_CMD_REPORT_STATUS &&
            (int)message.data == AEL_STATUS_ERROR_OPEN)
        {
            logw("[-!-] Restarting stream because of errors in http_stream");
            pn_pipeline_stop_all();

            audio_element_reset_state(mp3_decoder);
            audio_element_reset_state(i2s_stream_writer);

            pn_pipeline_reset_all();
            pn_pipeline_run();
            continue;
        }
    }

    logi("[ 10 ] Stoping the audio pipeline");
    pn_pipeline_destroy();

    logi("[-*-] Unregistering all audio elements in pipeline");
    pn_pipeline_unregister(http_stream_reader);
    pn_pipeline_unregister(mp3_decoder);
    pn_pipeline_unregister(i2s_stream_writer);

    logi("[-*-] Remove event listener from pipeline");
    pn_pipeline_remove_listener();

    logi("[ 11 ] Stopping periherals before removing event listener");
    stop_wifi_connection();

    logi("[ 12 ] Remove the event listener enitirely");
    audio_event_iface_destroy(event);

    logi("[ 13 ] Releasing all other allocated resources");
    pn_pipeline_deinit();
    audio_element_deinit(http_stream_reader);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(mp3_decoder);
    esp_periph_set_destroy(periph_set);
}
