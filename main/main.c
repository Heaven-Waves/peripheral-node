
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "peripheral_node/logs.h"

void app_main()
{
    char *msg = "log message";
    int i = 0;
    while (1)
    {
        logi("Hello world from %s %d", msg, i);
        i++;
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
