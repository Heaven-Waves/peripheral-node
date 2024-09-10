
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logs.h"

void app_main()
{
    int i = 0;
    while (1)
    {
        logi("Hello world from log message");
        i++;
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
