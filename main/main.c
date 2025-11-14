/*
 * ESP32-LyraT Opus Audio Receiver
 *
 * This application receives Opus-encoded audio over UDP multicast and plays it
 * through the I2S audio output (speakers/headphones).
 *
 * Features:
 * - UDP Multicast reception
 * - Opus decoding (48kHz stereo)
 * - Direct I2S audio output
 * - WiFi connection
 * - Ring buffer for smooth playback
 *
 * Hardware: ESP32-LyraT V4.3 (or compatible)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

// ESP-ADF includes
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_mem.h"
#include "audio_common.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "opus_decoder.h"
#include "board.h"
#include "ringbuf.h"

#include "opus.h"

#include "peripheral_node/logs.h"

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

#define BASIC_BUFFER_SIZE (8 * 1024)
#define RTP_HEADER_SIZE 12

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

static audio_element_handle_t i2s_writer;
static audio_event_iface_handle_t evt;
static ringbuf_handle_t in_ringbuf = NULL; // Ring buffer for feeding data to pipeline

OpusDecoder *decoder;
static int udp_socket = -1;
static volatile bool pipeline_running = false;

// Static buffers to avoid stack overflow
static int16_t pcm_buffer[2048];  // Max Opus frame size at 48kHz
static char udp_recv_buffer[512]; // Opus payload + header

// =============================================================================
// WIFI EVENT HANDLER
// =============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        logi("WiFi disconnected, reconnecting...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        logi("Got IP address:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// =============================================================================
// WIFI INITIALIZATION
// =============================================================================

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    // Configure WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    logi("WiFi initialization complete");

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        logi("Connected to WiFi SSID: %s", CONFIG_WIFI_SSID);
    }
}
void app_main(void)
{
    esp_err_t ret;

    logi("=================================================");
    logi("ESP32-LyraT Opus Audio Receiver");
    logi("=================================================");
    logi("Configuration:");
    logi("  Multicast: %s:%d", CONFIG_MULTICAST_IP, CONFIG_UDP_PORT);
    logi("  Sample Rate: %dHz", CONFIG_AUDIO_SAMPLE_RATE);
    logi("  Channels: %d", CONFIG_AUDIO_CHANNELS);
    logi("  Bits: %d", CONFIG_BITS_PER_SAMPLE);
    logi("=================================================");

    // 1. Initialize NVS (required for WiFi)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    logi("NVS initialized");

    // 2. Initialize audio board (LyraT)
    audio_hal_codec_config_t audio_codec_cfg = {
        .adc_input = AUDIO_HAL_ADC_INPUT_LINE1,
        .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,
        .codec_mode = AUDIO_HAL_CODEC_MODE_DECODE,
        .i2s_iface = {
            .mode = AUDIO_HAL_MODE_SLAVE,
            .fmt = AUDIO_HAL_I2S_NORMAL,
            .samples = AUDIO_HAL_48K_SAMPLES,
            .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
        }};

    logi("Audio board configuration set");

    audio_hal_handle_t audio_hal = audio_hal_init(&audio_codec_cfg, &AUDIO_CODEC_ES8388_DEFAULT_HANDLE);
    if (audio_hal == NULL)
    {
        loge("Failed to initialize audio board");
        return;
    }

    audio_hal_set_volume(audio_hal, 100);
    audio_hal_ctrl_codec(audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
    logi("Audio board initialized, volume: 100%%");

    // 3. Connect to WiFi
    wifi_init_sta();
    logi("WiFi connected");
}
