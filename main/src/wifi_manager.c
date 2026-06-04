/**
 * @file wifi_manager.c
 * @author Pratik Mistry
 * @brief Central unit to control wifi connections
 * @version 0.1
 * @date 2021-08-02
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "wifi_manager.h"
#include "flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <string.h>

static const char *TAG = "WiFi Manager";

/* Private Defines -----------------------------------------------------------*/
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_AUTH_FAIL_BIT BIT2
#define MAX_RETRY_SET 5

/* Private Variables ---------------------------------------------------------*/
static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;
static bool s_wifi_configured = false;
static bool s_is_connected = false;
bool wifi_1_min_flag = false;
static uint8_t s_reconnect_timeout_min = 0;
static bool s_is_sta_initialized = false;
bool is_incorrect_password = false;
/* Private Function Prototypes -----------------------------------------------*/
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

/* Public Function Implementation ------------------------------------------*/

void WIFI_MANAGER_init(void)
{
    wifi_config_t wifi_config = {0};
    char ssid[32] = {0};
    char password[64] = {0};
  
    // Attempt to read credentials from flash
    bool ssid_ok = (FLASH_read_file("ssid", ssid, sizeof(ssid)) == ESP_OK);
    bool pass_ok = (FLASH_read_file("pass", password, sizeof(password)) == ESP_OK);

    if (ssid_ok && pass_ok && strlen(ssid) > 0)
    {
        strcpy((char *)wifi_config.sta.ssid, ssid);
        strcpy((char *)wifi_config.sta.password, password);
        s_wifi_configured = true;
        ESP_LOGI(TAG, "Credentials found. Will attempt to connect.");
    }
    else
    {
        ESP_LOGW(TAG, "Wi-Fi not configured. SSID or password file not found.");
        ESP_LOGI(TAG, "Starting provisioning mode.");
        s_wifi_configured = false;
        // The provisioning module will be initialized by the system scheduler
    }

    if (s_wifi_configured)
    {
        s_wifi_event_group = xEventGroupCreate();

        esp_netif_t *netif = esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        esp_netif_set_hostname(netif, "MyESP32Device");
        s_is_sta_initialized = true;
        ESP_LOGI(TAG, "wifi_init_sta finished.");
    }

    ESP_LOGI(TAG, "Initialized");
}

esp_err_t WIFI_MANAGER_configure_and_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = {0};

    // If this is the first time we are configuring, we need to init the STA stack
    if (!s_is_sta_initialized) {
        s_wifi_event_group = xEventGroupCreate();

        // The network stack (esp_netif and event loop) is already initialized
        // by the provisioning module or by WIFI_MANAGER_init on boot.
        // We just need to create the STA interface.
        esp_netif_t *netif = esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));
        esp_netif_set_hostname(netif, "MyESP32Device");
        s_is_sta_initialized = true;
        ESP_LOGI(TAG, "STA stack initialized for the first time.");
    }

    // Set the new credentials
    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "Attempting to connect to Wi-Fi...");

    // Clear previous event bits
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_AUTH_FAIL_BIT);

    // Start Wi-Fi. The event handler will now pick up the WIFI_EVENT_STA_START
    // event and call esp_wifi_connect() for us.
    ESP_ERROR_CHECK(esp_wifi_start());

    // Mark as configured
    s_wifi_configured = true;

    // Wait for the connection to succeed or fail
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_AUTH_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP successfully!");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to connect to AP.");
        // Disconnect to stop further retry attempts from the event handler
        esp_wifi_disconnect();
        return ESP_FAIL;
    }
}

void WIFI_MANAGER_tasks(void)
{
    // This task is driven by the 1-minute tick
    if (wifi_1_min_flag)
    {
        wifi_1_min_flag = false;
        if (!s_is_connected && s_wifi_configured)
        {
            s_reconnect_timeout_min++;
            ESP_LOGI(TAG, "1-minute tick: Wi-Fi is disconnected. Attempting to reconnect.");
            esp_wifi_connect();
        }
        if (s_reconnect_timeout_min > 3)
        {
            ESP_LOGE(TAG, "Failed to reconnect to Wi-Fi after 3 minutes. Rebooting.");
            if (is_incorrect_password) {
                FLASH_delete_file("ssid");
                FLASH_delete_file("pass");
            }
            esp_restart();
        }
    }
}

void WIFI_MANAGER_tick_1min(void)
{
        wifi_1_min_flag = true;
}

/* Private Function Implementation -----------------------------------------*/

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        s_is_connected = false;
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "Disconnected from AP, reason: %d", disconnected->reason);

        // Check for authentication failure
        if (disconnected->reason == WIFI_REASON_AUTH_FAIL ||
            disconnected->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
            disconnected->reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
            ESP_LOGE(TAG, "Authentication failed. Incorrect password?");
            xEventGroupSetBits(s_wifi_event_group, WIFI_AUTH_FAIL_BIT);
            // Don't retry on auth fail
            is_incorrect_password = true;
            return;
        }

        if (s_retry_num < MAX_RETRY_SET)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Connection failed after max retries");
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_reconnect_timeout_min = 0;
        s_is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief indicates if the wifi credentials are configured or not.
 */
bool WIFI_MANAGER_is_configured(void)
{
    return s_wifi_configured;
}

/**
 * @brief indicates wifi connection status
 */
bool WIFI_MANAGER_is_connected(void)
{
    return s_is_connected;
}

/**
 * @brief returns the rssi value
 */
int WIFI_MANAGER_get_rssi(void)
{
    wifi_ap_record_t wifi_info;
    if (esp_wifi_sta_get_ap_info(&wifi_info) == ESP_OK)
    {
        return wifi_info.rssi;
    }
    return 0;
}

/**
 * @brief sets the wifi credentials by writing to flash.
 */
void WIFI_MANAGER_set_credentials(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Setting new Wi-Fi credentials.");
    FLASH_write_to_file("ssid", (char *)ssid);
    FLASH_write_to_file("pass", (char *)password);
    ESP_LOGI(TAG, "Credentials saved. Please reboot the device.");
}
