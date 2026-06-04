#include <stdio.h>
#include "system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"

void app_main(void)
{
    System_tasks();
}
