#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include <stdarg.h>
#include <stdio.h>

static const char *TAG = "PERIPHERAL_NODE";

void logi(const char *msg, ...)
{
    char buff[256];

    va_list args;
    va_start(args, msg);

    vsprintf(buff, msg, args);

    ESP_LOGI(TAG, "%s", buff);

    va_end(args);
}

void loge(const char *msg, ...)
{
    char buff[256];

    va_list args;
    va_start(args, msg);

    vsprintf(buff, msg, args);

    ESP_LOGE(TAG, "%s", buff);

    va_end(args);
}

void logw(char *msg, ...)
{
    char buff[256];

    va_list args;
    va_start(args, msg);

    vsprintf(buff, msg, args);

    ESP_LOGW(TAG, "%s", buff);

    va_end(args);
}