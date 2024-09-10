#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char *TAG = "PERIPHERAL_NODE";

void logi(char *msg)
{
    ESP_LOGI(TAG, "%s", msg);
}

void loge(char *msg)
{
    ESP_LOGE(TAG, "%s", msg);
}

void logw(char *msg)
{
    ESP_LOGW(TAG, "%s", msg);
}