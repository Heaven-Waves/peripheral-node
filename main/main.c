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
#define RTP_HEADER_SIZE (12)
#define WIFI_CONNECTED_BIT (BIT0)
#define I2S_PORT (I2S_NUM_0)

static EventGroupHandle_t s_wifi_event_group;

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

// =============================================================================
// UDP MULTICAST SETUP
// =============================================================================

static int setup_udp_multicast()
{
    struct sockaddr_in saddr;
    int sock;
    int err;

    logi("Setting up UDP multicast socket...");

    // Create UDP socket - following ESP-IDF multicast example
    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        loge("Failed to create socket: errno %d", errno);
        return -1;
    }

    // Bind socket to the port
    saddr.sin_family = PF_INET;
    saddr.sin_port = htons(CONFIG_UDP_PORT);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);

    err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
    if (err < 0)
    {
        loge("Failed to bind socket: errno %d", errno);
        close(sock);
        return -1;
    }
    logi("Socket bound to port %d", CONFIG_UDP_PORT);
    // Assign multicast address to imreq structure
    struct ip_mreq imreq = {0};
    imreq.imr_multiaddr.s_addr = inet_addr(CONFIG_MULTICAST_IP);
    imreq.imr_interface.s_addr = htonl(INADDR_ANY);

    // Join multicast group
    err = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof(struct ip_mreq));
    if (err < 0)
    {
        loge("Failed to join multicast group: errno %d", errno);
        close(sock);
        return -1;
    }
    logi("Successfully joined multicast group %s:%d", CONFIG_MULTICAST_IP, CONFIG_UDP_PORT);
    return sock;
}

// =============================================================================
// RTP HEADER PARSING
// =============================================================================

// RTP header structure (12 bytes minimum)
typedef struct
{
    uint8_t vpxcc; // V(2), P(1), X(1), CC(4)
    uint8_t mpt;   // M(1), PT(7)
    uint16_t seq;  // Sequence number
    uint32_t ts;   // Timestamp
    uint32_t ssrc; // SSRC
} rtp_header_t;

static inline int get_rtp_payload(
    uint8_t *packet,
    int packet_len,
    uint8_t **payload,
    int *payload_len)
{
    if (packet_len < RTP_HEADER_SIZE)
    {
        return -1; // Too short to be RTP
    }

    rtp_header_t *rtp = (rtp_header_t *)packet;

    // Check RTP version (should be 2)
    uint8_t version = (rtp->vpxcc >> 6) & 0x03;
    if (version != 2)
    {
        // Not RTP or wrong version, treat as raw Opus
        *payload = packet;
        *payload_len = packet_len;
        return 0;
    }

    // Calculate header size
    int header_size = RTP_HEADER_SIZE; // Basic RTP header

    // Add CSRC size if present
    uint8_t cc = rtp->vpxcc & 0x0F;
    header_size += cc * 4;

    // Check for extension header
    if (rtp->vpxcc & 0x10)
    {
        if (packet_len < header_size + 4)
        {
            return -1;
        }
        uint16_t ext_len = (packet[header_size + 2] << 8) | packet[header_size + 3];
        header_size += 4 + (ext_len * 4);
    }

    if (header_size >= packet_len)
    {
        return -1; // Invalid packet
    }

    // Extract payload
    *payload = packet + header_size;
    *payload_len = packet_len - header_size;

    return 1; // RTP packet successfully parsed
}

// =============================================================================
// UDP RECEIVER TASK
// =============================================================================

static void udp_receiver_task(void *pvParameters)
{
    struct sockaddr_in raddr;
    socklen_t socklen = sizeof(raddr);
    uint32_t packets_received = 0;
    uint32_t last_report_time = 0;

    logi("UDP receiver task started");

    // Wait for pipeline to start
    while (!pipeline_running)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    logi("Waiting for UDP packets on %s:%d", CONFIG_MULTICAST_IP, CONFIG_UDP_PORT);

    // Main receiving loop
    while (1)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(udp_socket, &rfds);

        struct timeval tv = {
            .tv_sec = 2,
            .tv_usec = 0,
        };

        // Wait for data with timeout
        int s = select(udp_socket + 1, &rfds, NULL, NULL, &tv);
        if (s < 0)
        {
            loge("Select failed: errno %d", errno);
            break;
        }
        else if (s == 0)
        {
            logw("Timeout - no data received, continue waiting");
            // Timeout - no data received, continue waiting
            continue;
        }

        // Data is available, receive it (using static buffer to avoid stack overflow)
        int len = recvfrom(udp_socket, udp_recv_buffer, sizeof(udp_recv_buffer) - 1, 0,
                           (struct sockaddr *)&raddr, &socklen);

        if (len < 0)
        {
            loge("recvfrom failed: errno %d", errno);
            continue;
        }

        packets_received++;

        // Parse RTP header to get Opus payload
        uint8_t *opus_payload = NULL;
        int opus_len = 0;
        int rtp_result = get_rtp_payload((uint8_t *)udp_recv_buffer, len, &opus_payload, &opus_len);

        if (rtp_result < 0)
        {

            logw("Invalid packet");
            continue; // Invalid packet
        }

        // Skip too small packets
        if (opus_len < 20)
        {
            logw("Packet too small (%d bytes), skipping", opus_len);
            continue;
        }

        logi("Before decoding");

        if (!decoder)
        {
            loge("opus decoder is NULL!");
            continue;
        }

        int no_fec = 0; // 0 = no FEC
        // // Decode Opus to PCM using static buffer
        int decoded_samples = opus_decode(
            decoder,
            opus_payload,
            opus_len,
            pcm_buffer,
            CONFIG_OPUS_FRAME_SIZE,
            no_fec);

        if (decoded_samples < 0)
        {
            logw("Opus decode error: %d", decoded_samples);
            continue;
        }

        // Calculate bytes to write
        int pcm_bytes = decoded_samples * CONFIG_AUDIO_CHANNELS * sizeof(int16_t);

        // Write PCM data to I2S
        int written = rb_write(
            in_ringbuf,
            (char *)pcm_buffer,
            pcm_bytes,
            portMAX_DELAY);

        if (written != pcm_bytes && packets_received <= 5)
        {
            logw("Buffer write incomplete: %d/%d bytes", written, pcm_bytes);
        }

        // Log statistics every 0.5 seconds
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_report_time >= 500)
        {
            logi("Received %lu packets, written: %d bytes", packets_received, written);

            logi("Decoded samples: %d", decoded_samples);
            // print original packet first 8 bytes in hex
            logi("Original packet first 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
                 (uint8_t)udp_recv_buffer[0], (uint8_t)udp_recv_buffer[1], (uint8_t)udp_recv_buffer[2], (uint8_t)udp_recv_buffer[3],
                 (uint8_t)udp_recv_buffer[4], (uint8_t)udp_recv_buffer[5], (uint8_t)udp_recv_buffer[6], (uint8_t)udp_recv_buffer[7]);

            // print opus_payload first 8 bytes in hex
            logi("Opus payload first 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
                 opus_payload[0], opus_payload[1], opus_payload[2], opus_payload[3],
                 opus_payload[4], opus_payload[5], opus_payload[6], opus_payload[7]);

            logi("Decoded samples: %d", decoded_samples);
            logi("PCM first 8 samples: %02X %02X %02X %02X %02X %02X %02X %02X",
                 (uint16_t)pcm_buffer[0], (uint16_t)pcm_buffer[1], (uint16_t)pcm_buffer[2], (uint16_t)pcm_buffer[3],
                 (uint16_t)pcm_buffer[4], (uint16_t)pcm_buffer[5], (uint16_t)pcm_buffer[6], (uint16_t)pcm_buffer[7]);

            int rb_filled = rb_bytes_filled(in_ringbuf);
            int rb_avail = rb_bytes_available(in_ringbuf);

            logi("Raw ring buffer: filled %d bytes, available %d bytes",
                 rb_filled, rb_avail);

            audio_element_state_t i2s_state = audio_element_get_state(i2s_writer);
            logw("I2S State: %d", i2s_state);

            last_report_time = now;
        }
    }

    vTaskDelete(NULL);
}

// =============================================================================
// AUDIO PIPELINE SETUP
// =============================================================================

static esp_err_t setup_audio_pipeline(void)
{
    logi("Setting up audio pipeline...");

    // Create opus decoder
    int opus_decoder_err;
    decoder = opus_decoder_create(
        CONFIG_AUDIO_SAMPLE_RATE,
        CONFIG_AUDIO_CHANNELS,
        &opus_decoder_err);
    if (opus_decoder_err != OPUS_OK || decoder == NULL)
    {
        loge("Failed to create Opus decoder: %d", opus_decoder_err);
        return ESP_FAIL;
    }
    logi("Opus decoder created successfully");

    // Create input ring buffer
    in_ringbuf = rb_create(BASIC_BUFFER_SIZE, 1);

    if (in_ringbuf == NULL)
    {
        loge("Failed to create input ring buffer");
        return ESP_FAIL;
    }
    logi("Input ring buffer created successfully");

    // Configure I2S stream writer
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(
        I2S_PORT,
        CONFIG_AUDIO_SAMPLE_RATE,
        CONFIG_BITS_PER_SAMPLE,
        AUDIO_STREAM_WRITER);
    i2s_cfg.stack_in_ext = true;
    i2s_cfg.task_stack = BASIC_BUFFER_SIZE;
    i2s_cfg.out_rb_size = BASIC_BUFFER_SIZE;
    i2s_cfg.task_core = 0;
    i2s_cfg.volume = 100;

    i2s_writer = i2s_stream_init(&i2s_cfg);
    if (i2s_writer == NULL)
    {
        loge("Failed to create I2S writer");
        return ESP_FAIL;
    }

    int ret = audio_element_set_input_ringbuf(i2s_writer, in_ringbuf);

    if (ret != ESP_OK)
    {
        loge("Failed to set input ring buffer for I2S writer");
        return ESP_FAIL;
    }

    logi("Audio pipeline setup complete");
    return ESP_OK;
}

// =============================================================================
// MAIN APPLICATION ENTRY POINT
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

    // 4. Setup UDP multicast socket
    udp_socket = setup_udp_multicast();
    if (udp_socket < 0)
    {
        loge("Failed to setup UDP socket");
        return;
    }

    // 5. Setup audio pipeline
    ret = setup_audio_pipeline();
    if (ret != ESP_OK)
    {
        loge("Failed to setup audio pipeline");
        return;
    }
    logi("Audio pipeline ready");

    // 6. Start audio pipeline
    ret = audio_element_run(i2s_writer);
    if (ret != ESP_OK)
    {
        loge("Failed to start I2S writer: %d", ret);
        return;
    }

    ret = audio_element_resume(i2s_writer, 0, 0);
    if (ret != ESP_OK)
    {
        loge("Failed to resume I2S writer: %d", ret);
        return;
    }

    pipeline_running = true;
    logi("Pipeline running");

    // 7. Start UDP receiver task
    xTaskCreatePinnedToCore(udp_receiver_task, "udp_rx", 16384, NULL, 5, NULL, 1);

    logi("=================================================");
    logi("System ready! Listening for Opus audio streams...");
    logi("=================================================");

    // Main loop - just monitor memory
    while (1)
    {
        logi("Free heap: %lu bytes, Min free: %lu bytes",
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(30000)); // Log every 30 seconds
    }
}
