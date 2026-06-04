/**
 * @file time_management.c
 * @author your name
 * @brief Manages system time, including getting/setting time and tracking runtime.
 * @version 0.1
 * @date 2021-08-30
 *
 * @copyright Copyright (c) 2021
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "time_management.h"
#include <sys/time.h>
#include <string.h>
#include "esp_log.h"
#include "wifi_manager.h"
/* Private variables ---------------------------------------------------------*/
/**
 * @brief Structure to hold time-related data.
 */
typedef struct
{
    struct timeval current_time; /**< Holds seconds and microseconds. */
    struct tm *tm_info;          /**< Pointer to broken-down time structure. */
    char time_buffer[26];        /**< Buffer for formatted time string. */
} my_time_t;

my_time_t my_time; /**< Global instance for current time data. */

my_time_t updated_time; /**< Global instance for staging time updates. */

uint32_t runtime_mins; /**< Counter for device runtime in minutes. */

uint32_t wifi_connected_mins; /**< Counter for Wi-Fi connected time in minutes. */

uint32_t wifi_disconnected_mins; /**< Counter for Wi-Fi disconnected time in minutes. */

static const char *TAG_Time = "Time Management";
/* Private Functions ---------------------------------------------------------*/
/* Code Implementation -------------------------------------------------------*/
/**
 * Initialise this layer
 */
void TIME_MANAGEMENT_init()
{
    runtime_mins = 0;
    memset(&my_time, 0, sizeof(my_time));
    // CRITICAL: This is a blocking busy-wait. If time is not synced (e.g. no NTP),
    // it will block the main task forever, and the terminal will never start.
    // while (my_time.current_time.tv_sec == 0)
    //     gettimeofday(&my_time.current_time, NULL);
    ESP_LOGI(TAG_Time, "Initialized :)\n");
}

/**
 * @brief Tasks function for the time management module.
 */
void TIME_MANAGEMENT_tasks()
{
    // Currently, no tasks are defined here.
    // Future time-related tasks can be added as needed.
}

/**
 * @brief Tick function called every minute to update the runtime counter.
 */

void TIME_MANAGEMENT_tick_1min()
{
    runtime_mins++;
    if (WIFI_MANAGER_is_connected())
    {
        wifi_connected_mins++;
    }
    else
    {
        wifi_disconnected_mins++;
    }
}
/**
 * @brief Gets the current system time as a formatted string.
 *
 * @return char* string formated time of day
 */
void TIME_MANAGEMENT_get_time_string(char *buffer, size_t buffer_size)
{
    struct timeval tv;
    struct tm timeinfo;
    // Get the current time to ensure the string is not stale.
    gettimeofday(&tv, NULL);
    // Use the re-entrant (thread-safe) version of localtime
    localtime_r(&tv.tv_sec, &timeinfo);
    strftime(buffer, buffer_size, "%Y/%m/%d %H:%M:%S", &timeinfo);
}
/**
 * @brief Gets the current time as milliseconds since the Unix epoch.
 *
 * @return uint32_t returns the current epoch time
 */
long long TIME_MANAGEMENT_get_epoch_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    // Use LL suffix for long long constants to prevent potential overflow
    return (long long)tv.tv_sec * 1000LL + (long long)tv.tv_usec / 1000LL;
}
/**
 * @brief Sets the system time from a Unix epoch time in milliseconds.
 *
 * @param epoch
 */
void TIME_MANAGEMENT_set_epoch_time(long long epoch)
{
    updated_time.current_time.tv_sec = epoch / 1000;
    updated_time.current_time.tv_usec = (epoch % 1000) * 1000;
    settimeofday(&updated_time.current_time, NULL);
    ESP_LOGI(TAG_Time, "System time updated to epoch: %lld ms", epoch);
    TIME_MANAGEMENT_get_time_string(updated_time.time_buffer, sizeof(updated_time.time_buffer));
    ESP_LOGI(TAG_Time, "Updated time string: %s", updated_time.time_buffer);
}
/**
 * @brief Gets the operational age of the device since power-up.
 *
 * @return uint32_t returns runtime in minutes
 */
uint32_t TIME_MANAGEMENT_get_runtime_in_minutes()
{
    return runtime_mins;
}

/**
 * @brief operational wifi connected time in minutes
 *
 * @return uint32_t
 */
uint32_t TIME_MANAGEMENT_get_wifi_connected_minutes()
{
    return wifi_connected_mins;
}

/**
 * @brief operational wifi disconnected time in minutes
 *
 * @return uint32_t
 */
uint32_t TIME_MANAGEMENT_get_wifi_disconnected_minutes()
{
    return wifi_disconnected_mins;
}

/* END OF FILE ---------------------------------------------------------------*/