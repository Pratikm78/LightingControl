/**
 * @file ota.c
 * @author Your Name
 * @brief Source file for the ota module.
 * @version 0.1
 * @date 2025-11-03
 * @note This file was updated to fix a version comparison bug.
 *
 * @copyright Copyright (c) 2025
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "system.h"
#include <stdio.h>
#include "wifi_manager.h"
#include "terminal_console.h" // Include for de-init function

/* Private Defines -----------------------------------------------------------*/
static const char *TAG = "Ota";

/* Private Variables ---------------------------------------------------------*/
static bool s_ota_check_pending = false;
static int s_total_size = 0;
static int s_downloaded_size = 0;
static int s_last_percentage = -1;

/* Private Function Prototypes -----------------------------------------------*/
/**
 * @brief Event handler for the OTA HTTP client to add the authorization header.
 */
static esp_err_t _ota_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "OTA download request: Connected, adding headers.");
        char auth_header[128];
        snprintf(auth_header, sizeof(auth_header), "token %s", CONFIG_OTA_GITHUB_TOKEN);
        esp_http_client_set_header(evt->client, "Authorization", auth_header);
        // This header is crucial for telling GitHub we want the binary file
        esp_http_client_set_header(evt->client, "Accept", "application/octet-stream");
        s_downloaded_size = 0; // Reset progress on new connection
        s_last_percentage = -1;
        break;
    case HTTP_EVENT_ON_DATA:
        if (s_total_size > 0) {
            s_downloaded_size += evt->data_len;
            int progress = (s_downloaded_size * 100) / s_total_size;
            // Dampen updates to every 15% to prevent UART FIFO overflow and network task starvation
            if (progress >= s_last_percentage + 15 || progress == 100 || s_last_percentage == -1) {
                s_last_percentage = progress;
                ESP_LOGI(TAG, "OTA Progress: %d%% (%d / %d bytes)", progress, s_downloaded_size, s_total_size);
            }
        }
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGI(TAG, "OTA download request: Redirected, adding headers for new location.");
        // For private repos, we must re-apply the Authorization header to the redirected URL
        snprintf(auth_header, sizeof(auth_header), "token %s", CONFIG_OTA_GITHUB_TOKEN);
        esp_http_client_set_header(evt->client, "Authorization", auth_header);
        esp_http_client_set_header(evt->client, "Accept", "application/octet-stream");
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "OTA download finished.");
        break;
    default:
        break;
    }
    return ESP_OK;
}


static void ota_check_for_update(void);
/**
 * @brief Compares two version strings (e.g., "v1.2.3") to check if a new version is available.
 *
 * @param latest_version The version string from the remote source (e.g., GitHub release).
 * @param running_version The version string of the currently running application.
 * @return true if latest_version is greater than running_version.
 * @return false otherwise.
 */
static bool is_new_version_available(const char *latest_version, const char *running_version)
{
    int latest_major = 0, latest_minor = 0, latest_patch = 0;
    int running_major = 0, running_minor = 0, running_patch = 0;

    // Parse version strings: "v<major>.<minor>.<patch>"
    sscanf(latest_version, "v%d.%d.%d", &latest_major, &latest_minor, &latest_patch); // GitHub tags include 'v'
    sscanf(running_version, "%d.%d.%d", &running_major, &running_minor, &running_patch); // Firmware version does not

    if (latest_major > running_major) {
        return true;
    }
    if (latest_major == running_major && latest_minor > running_minor) {
        return true;
    }
    if (latest_major == running_major && latest_minor == running_minor && latest_patch > running_patch) {
        return true;
    }
    return false;
}

/* Public Function Implementation ------------------------------------------*/

void OTA_init(void)
{
    ESP_LOGI(TAG, "Initialized");
}

void OTA_tasks(void)
{
    if (s_ota_check_pending && System_is_connected()) {
        s_ota_check_pending = false; // Consume the flag
        ota_check_for_update();
    }
}

void OTA_tick_1hr(void)
{
    s_ota_check_pending = true;
}

/* Private Function Implementation -----------------------------------------*/
static void ota_check_for_update(void)
{
    ESP_LOGI(TAG, "Checking for new firmware version...");

    char ota_url_buffer[256];
    snprintf(ota_url_buffer, sizeof(ota_url_buffer), "https://api.github.com/repos/%s/%s/releases/latest",
             CONFIG_OTA_GITHUB_USERNAME, CONFIG_OTA_GITHUB_REPO_NAME);
    //printf("OTA URL: %s\n", ota_url_buffer);
    esp_http_client_config_t config = {
        .url = ota_url_buffer,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
        .user_agent = "esp32-ota-client/1.0",
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Construct and add the Authorization header for private repositories
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "token %s", CONFIG_OTA_GITHUB_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth_header);


    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0) {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        } else {
            char *buffer = malloc(content_length + 1);
            if (buffer == NULL) {
                ESP_LOGE(TAG, "Cannot malloc http buffer");
            } else {
                int read_len = esp_http_client_read_response(client, buffer, content_length);
                if (read_len <= 0) {
                    ESP_LOGE(TAG, "Error reading response");
                } else {
                    buffer[read_len] = '\0';

                    cJSON *root = cJSON_Parse(buffer);
                    if (root)
                    {
                        //printf("JSON response: %s\n", buffer);
                        ESP_LOGI(TAG, "Successfully parsed JSON response.");
                        cJSON *tag_name = cJSON_GetObjectItem(root, "tag_name");
                        cJSON *assets = cJSON_GetObjectItem(root, "assets");

                        if (cJSON_IsString(tag_name) && cJSON_IsArray(assets) && cJSON_GetArraySize(assets) > 0)
                        {
                            const char *latest_version = tag_name->valuestring;
                            const esp_app_desc_t *running_app_info = esp_app_get_description();

                            ESP_LOGI(TAG, "Running version: %s, Latest version: %s", running_app_info->version, latest_version);

                            // Correctly compare semantic versions
                            if (is_new_version_available(latest_version, running_app_info->version)) {
                                ESP_LOGI(TAG, "New version available: %s", latest_version);
                                
                                cJSON *asset = cJSON_GetArrayItem(assets, 0); // Assuming the .bin is the first asset
                                cJSON *download_url_json = cJSON_GetObjectItem(asset, "url"); // Use the API URL for the asset
                                cJSON *size_json = cJSON_GetObjectItem(asset, "size");

                                if (cJSON_IsNumber(size_json)) {
                                    s_total_size = size_json->valueint;
                                    ESP_LOGI(TAG, "Firmware size: %d bytes", s_total_size);
                                }

                                if (cJSON_IsString(download_url_json))
                                {
                                    const char *firmware_url = download_url_json->valuestring;
                                    ESP_LOGI(TAG, "Firmware URL: %s", firmware_url);

                                    esp_http_client_config_t ota_client_config = {
                                        .url = firmware_url,
                                        .crt_bundle_attach = esp_crt_bundle_attach,
                                        .timeout_ms = 120000, // Increased timeout for firmware download
                                        .buffer_size = 200, // Increased buffer size for large headers
                                        .buffer_size_tx = 200,
                                        .disable_auto_redirect = false, // Follow redirects
                                        .event_handler = _ota_http_event_handler,
                                        .keep_alive_enable = true,
                                    };

                                    esp_https_ota_config_t https_ota_config = { .http_config = &ota_client_config };

                                    ESP_LOGI(TAG, "Starting OTA update...");
                                    esp_err_t ota_err = esp_https_ota(&https_ota_config); // This is a blocking call
                                    if (ota_err == ESP_OK) {
                                        ESP_LOGI(TAG, "OTA update successful! Rebooting...");
                                        // De-initialize the console to prevent it from freezing after restart
                                        TERMINAL_CONSOLE_deinit();
                                        esp_restart();
                                    } else if (ota_err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                                        ESP_LOGW(TAG, "OTA in progress, another OTA is not allowed");
                                    } else {
                                        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(ota_err));
                                    }
                                }
                                else
                                {
                                    ESP_LOGE(TAG, "Could not find 'browser_download_url' in JSON asset.");
                                }
                            } else {
                                ESP_LOGI(TAG, "Firmware is up to date.");
                            }
                        }
                        else
                        {
                            ESP_LOGE(TAG, "Failed to parse 'tag_name' or 'assets' from JSON.");
                        }
                        cJSON_Delete(root);
                    } else {
                        ESP_LOGE(TAG, "Failed to parse JSON response body.");
                    }
                }
                free(buffer);
            }
        }
    }
    esp_http_client_cleanup(client);
}
/**
 * @brief  Force an immediate check for firmware update.
 * 
 */
void OTA_force_check_for_update(void)
{
    ota_check_for_update();
}