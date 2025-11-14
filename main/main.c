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
// static const int WIFI_CONNECTED_BIT = BIT0;

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
}
