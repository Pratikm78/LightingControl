/**
 * @file auto_time_sync.c
 * @author Your Name
 * @brief Source file for the auto_time_sync module.
 * @version 0.1
 * @date 2025-10-16
 *
 * @copyright Copyright (c) 2025
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "auto_time_sync.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "wifi_manager.h"
#include "time_management.h"
#include "esp_crt_bundle.h"
#include <stdlib.h> // For setenv
#include <time.h>   // For tzset
#include "system.h"
/* Private Defines -----------------------------------------------------------*/
static const char *TAG = "Auto Time Sync";

/* Private Variables ---------------------------------------------------------*/
static bool s_time_sync_triggered = false;
static bool AUTO_TIME_SYNC_tick_1min_flag = true;

typedef struct {
    char *buffer;
    size_t max_len;
} http_user_data_t;

/* Private Function Prototypes -----------------------------------------------*/
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->user_data) {
                http_user_data_t *user_data = (http_user_data_t *)evt->user_data;
                char *buffer = user_data->buffer;
                size_t current_len = strlen(buffer);
                size_t max_len = user_data->max_len;

                if (current_len + evt->data_len < max_len) {
                    memcpy(buffer + current_len, evt->data, evt->data_len);
                    buffer[current_len + evt->data_len] = '\0';
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static esp_err_t get_timezone_from_ip(char *timezone_str, size_t timezone_len)
{
    char response_buffer[128] = {0};
    http_user_data_t user_data = { .buffer = response_buffer, .max_len = sizeof(response_buffer) };
    esp_http_client_config_t config = {
        .url = "http://ip-api.com/json/?fields=timezone",
        .event_handler = _http_event_handler,
        .user_data = &user_data,
        .buffer_size = sizeof(response_buffer),
        .timeout_ms = 5000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
   // printf("IP API response: %s\n", response_buffer);
    cJSON *root = cJSON_Parse(response_buffer);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    cJSON *timezone = cJSON_GetObjectItem(root, "timezone");
    if (cJSON_IsString(timezone) && (timezone->valuestring != NULL))
    {
        strncpy(timezone_str, timezone->valuestring, timezone_len - 1);
        ESP_LOGI(TAG, "Detected timezone: %s", timezone_str);
        err = ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to parse timezone from JSON");
        err = ESP_FAIL;
    }

    cJSON_Delete(root);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t sync_time_from_timeapi_io(const char *timezone_arg)
{
    char url[128];
    snprintf(url, sizeof(url), "https://timeapi.io/api/v1/time/current/zone?timezone=%s", timezone_arg);
    
    char response_buffer[512] = {0};
    http_user_data_t user_data = { .buffer = response_buffer, .max_len = sizeof(response_buffer) };
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = &user_data,
        .buffer_size = 512,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP GET request to timeapi.io failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        s_time_sync_triggered = false; // Allow retrying on next opportunity
        return err;
    }

    //printf("Time API response: %s\n", response_buffer);
    cJSON *root = cJSON_Parse(response_buffer);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON response from timeapi.io: %s", response_buffer);
        esp_http_client_cleanup(client);
        s_time_sync_triggered = false; // Allow retrying on next opportunity
        return ESP_FAIL;
    }

    cJSON *date_time_json = cJSON_GetObjectItem(root, "date_time");

    if (cJSON_IsString(date_time_json) && (date_time_json->valuestring != NULL)) {
        struct tm tm = {0};
        if (sscanf(date_time_json->valuestring, "%d-%d-%dT%d:%d:%d",
                   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                   &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            tm.tm_isdst = -1; // Let mktime decide based on TZ

            time_t t = mktime(&tm);
            TIME_MANAGEMENT_set_epoch_time((long long)t * 1000);
            ESP_LOGI(TAG, "Time successfully synced from timeapi.io");
            err = ESP_OK;
        } else {
            ESP_LOGE(TAG, "Failed to parse date_time string: %s", date_time_json->valuestring);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Failed to parse 'date_time' from JSON response");
        s_time_sync_triggered = false; // Allow retrying on next opportunity
        err = ESP_FAIL;
    }

    cJSON_Delete(root);
    esp_http_client_cleanup(client);
    return err;
}

/* Public Function Implementation ------------------------------------------*/

void AUTO_TIME_SYNC_init(void)
{
    ESP_LOGI(TAG, "Initialized");
}

void AUTO_TIME_SYNC_tasks(void)
{
    // This task will be called in a loop.
    // We only want to trigger the sync once when Wi-Fi connects.
    if (System_is_connected() && !s_time_sync_triggered && AUTO_TIME_SYNC_tick_1min_flag)
    {
        s_time_sync_triggered = true; // Prevent re-triggering
        AUTO_TIME_SYNC_tick_1min_flag = false;
        char timezone[64] = {0};
        if (get_timezone_from_ip(timezone, sizeof(timezone)) == ESP_OK) {
            // Set the Timezone environment variable for the system.
            // This allows `localtime_r` to correctly convert from UTC system time.
            setenv("TZ", timezone, 1);
            tzset();
            ESP_LOGI(TAG, "Timezone set to: %s", timezone);
            sync_time_from_timeapi_io(timezone);
        } else {
            ESP_LOGW(TAG, "Could not determine timezone from IP. Attempting sync with UTC.");
            // Fallback to UTC if IP lookup fails
            setenv("TZ", "UTC", 1);
            tzset();
            sync_time_from_timeapi_io("UTC");
        }
    }
}

void AUTO_TIME_SYNC_tick_1min(void)
{
    AUTO_TIME_SYNC_tick_1min_flag = true;
}
