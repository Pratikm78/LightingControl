/**
 * @file system.c
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2023-08-29
 *
 * @copyright Copyright (c) 2023
 * @note This file contains a system-level tick and task manager.
 *
 */

/*Includes*/
#include "system.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time_management.h"   // Include the time management header
#include "terminal_console.h"  // Include the terminal console header
#include "flash.h"             // Include the flash header
#include "wifi_manager.h"      // Include the wifi manager header
#include "wifi_provisioning.h" // Include the wifi provisioning header
#include "auto_time_sync.h"    // Include the auto time sync header
#include "ota.h"               // Include the OTA header
#include "mqtt.h"              // Include the MQTT header
#include "lte.h"               // Include the LTE header
#include "data_usage.h"        // Include the Data Usage header
#include "light_control.h"     // Include the Light Control header
#include "esp_event.h"
#include "button.h"
#include "driver/gpio.h"
#include "esp_netif.h"

/*Defines*/
#define TIMER_1MS_DIVIDER 100 // 1ms tick, so 100 ticks = 100ms
/*Variables*/
/**
 * @brief Structure to hold system tick counters and flags.
 */
typedef struct
{
    uint32_t counter_1ms;   /**< Millisecond counter, rolls over at TIMER_1MS_DIVIDER. */
    uint32_t counter_100ms; /**< 100-millisecond counter, rolls over at 10. */
    uint32_t counter_1s;    /**< Second counter, rolls over at 60. */
    uint32_t counter_1min;  /**< Minute counter, rolls over at 10. */
    uint32_t counter_10min; /**< 10-minute counter, rolls over at 6. */
    uint32_t counter_1hr;
    bool flag_100ms; /**< Flag set every 100 milliseconds. */
    bool flag_1s;    /**< Flag set every second. */
    bool flag_1min;  /**< Flag set every minute. */
    bool flag_10min; /**< Flag set every 10 minutes. */
    bool flag_1hr;   /**< Flag set every hour. */
    bool flag_3hr;   /**< Flag set every 3 hours. */
} timer_keeper_t;

timer_keeper_t sys_tick; /**< Global instance of the system tick keeper. */

static esp_timer_handle_t sys_timer_handle;

/*Code*/
/** @brief Callback function for the periodic esp_timer. Calls the 1ms system tick. */
void timer_callback(void *args)
{
    System_tick_1ms();
}
/**
 * Initialise this layer
 */
void System_init()
{
    memset(&sys_tick, 0x00, sizeof(sys_tick));

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize the underlying networking stack and default event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const esp_timer_create_args_t esp_timer_create_args = {
        .callback = timer_callback,
        .name = "My timer"};
    ESP_ERROR_CHECK(esp_timer_create(&esp_timer_create_args, &sys_timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(sys_timer_handle, 1000));
}

/**
 * @brief Main cooperative task loop for the system.
 * @note This function acts as a simple scheduler, initializing modules and then running their tasks in a loop.
 */
void System_tasks()
{
    system_states_e sys_states = SYSTEM_INIT;
    while (1)
    {
        switch (sys_states)
        {
        case SYSTEM_INIT:
            System_init(); // Initialize the system
            sys_states = SYSTEM_TIME;
            break;
        case SYSTEM_TIME:
            TIME_MANAGEMENT_init(); // Initialize the time management module
            sys_states = TERMINAL_CONSOLE_INIT;
            break;
        case TERMINAL_CONSOLE_INIT:
            TERMINAL_CONSOLE_init(); // Initialize the terminal console module
            sys_states = FLASH_INIT;
            break;
        case FLASH_INIT:
            FLASH_init(); // Initialize the flash module
            sys_states = LTE_INIT;
            break;
        case LTE_INIT:
         //   LTE_init(); // Initialize the LTE module
            sys_states = WIFI_MANAGER_INIT;
            break;
        case WIFI_MANAGER_INIT:
            WIFI_MANAGER_init();                 // Initialize the wifi manager module
            sys_states = WIFI_PROVISIONING_INIT; // Always proceed to check provisioning
            break;
        case WIFI_PROVISIONING_INIT:
            // Only start provisioning if Wi-Fi is not already configured
            if (!WIFI_MANAGER_is_configured())
            {
                //     WIFI_PROVISIONING_init();
            }
            sys_states = AUTO_TIME_SYNC_INIT; // Proceed to auto time sync initialization
            break;
        case AUTO_TIME_SYNC_INIT:
            AUTO_TIME_SYNC_init(); // Initialize the auto time sync module
            sys_states = OTA_INIT;
            break;
        case OTA_INIT:
            OTA_init();             // Initialize the OTA module.
            sys_states = MQTT_INIT; // Proceed to MQTT initialization
            break;    
        case MQTT_INIT:
            MQTT_init();              // Initialize the MQTT module.
            sys_states = LIGHT_CONTROL_INIT_STATE; // Transition to new module
            break;
        case LIGHT_CONTROL_INIT_STATE:
            LIGHT_CONTROL_init();
            sys_states = SYSTEM_TIMER;
            break;
        case SYSTEM_TIMER:
            if (sys_tick.flag_100ms)
            {
                sys_tick.flag_100ms = false; // Consume the flag
            }
            if (sys_tick.flag_1s)
            {
                sys_tick.flag_1s = false; // Consume the flag

                

                MQTT_tick_1sec();
              //  LTE_tick_1s();
            //    BUTTON_tick_1s();
            }
            if (sys_tick.flag_1min)
            {
                sys_tick.flag_1min = false;  // Consume the flag
                TIME_MANAGEMENT_tick_1min(); // Call the 1-minute tick function
                WIFI_MANAGER_tick_1min();    // Call the WiFi manager 1-minute tick function
                AUTO_TIME_SYNC_tick_1min();  // Call the auto time sync 1-minute tick function
                MQTT_tick_1min();
            }
            if (sys_tick.flag_10min)
            {
                sys_tick.flag_10min = false; // Consume the flag
           ///     LTE_tick_10min(); // Call the LTE 10-minute tick function
            }
            if (sys_tick.flag_1hr)
            {
                sys_tick.flag_1hr = false; // Consume the flag
                OTA_tick_1hr();            // Call the OTA 1-hour tick function
            }
            if (sys_tick.flag_3hr)
            {
                sys_tick.flag_3hr = false; // Consume the flag
           //     LTE_tick_3hr();            // Call the OTA 3-hour tick function
            }
            sys_states = SYSTEM_MAIN;
            break;
        case SYSTEM_MAIN:
            TERMINAL_CONSOLE_tasks();
            FLASH_tasks();
            TIME_MANAGEMENT_tasks();
            WIFI_MANAGER_tasks();
            AUTO_TIME_SYNC_tasks();
          //  LTE_tasks();
            //OTA_tasks();
            MQTT_tasks();
            LIGHT_CONTROL_tasks();
           // BUTTON_tasks();
            //   DATA_USAGE_tasks();
            // Add a small delay to prevent this loop from hogging the CPU.
            // This delay is crucial. It allows lower-priority tasks (like WiFi) to run.
            vTaskDelay(pdMS_TO_TICKS(10));
            sys_states = SYSTEM_TIMER;
            break;

        default:
            break;
        }
    }
}

/**
 * @brief System tick function, called every 1ms from a timer ISR.
 */

void System_tick_1ms()
{

    sys_tick.counter_1ms++;

    if (sys_tick.counter_1ms >= TIMER_1MS_DIVIDER)
    {
        sys_tick.counter_100ms++;
        sys_tick.counter_1ms = 0;
        sys_tick.flag_100ms = true;
    }

    if (sys_tick.counter_100ms >= 10)
    {
        sys_tick.counter_1s++;
        sys_tick.counter_100ms = 0;
        sys_tick.flag_1s = true;
    }
    if (sys_tick.counter_1s >= 60)
    {
        sys_tick.counter_1min++;
        sys_tick.counter_1s = 0;
        sys_tick.flag_1min = true;
    }
    if (sys_tick.counter_1min >= 10)
    {
        sys_tick.counter_10min++;
        sys_tick.counter_1min = 0;
        sys_tick.flag_10min = true;
    }
    if (sys_tick.counter_10min >= 6)
    {
        sys_tick.counter_10min = 0;
        sys_tick.flag_1hr = true;
        sys_tick.counter_1hr++;
    }
    if (sys_tick.counter_1hr >= 3)
    {
        sys_tick.counter_1hr = 0;
        sys_tick.flag_3hr = true;
    }
}

bool System_is_connected(void)
{
    return WIFI_MANAGER_is_connected() || LTE_is_ready();
}