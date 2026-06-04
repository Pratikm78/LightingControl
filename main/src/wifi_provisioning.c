/**
 * @file wifi_provisioning.c
 * @author Your Name
 * @brief Source file for the wifi_provisioning module.
 * @version 0.1
 * @date 2025-10-09
 *
 * @copyright Copyright (c) 2025
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "wifi_provisioning.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_netif.h"
#include <string.h>
#include "lwip/dns.h"
#include "lwip/sockets.h"
#include "esp_mac.h"
#include "lwip/ip_addr.h"
#include "system.h" // For pause_timer, resume_timer
#include "wifi_manager.h"
#include "flash.h" // For MAX_FILEPATH_LENGTH

/* Private Defines -----------------------------------------------------------*/
static const char *TAG = "WiFi Provisioning";

/* Private Variables ---------------------------------------------------------*/
static char s_wifi_json_buffer[1024]; //!< Buffer to store the JSON list of scanned Wi-Fi networks.
static bool s_http_server_started = false; //!< Flag to ensure the HTTP server is only started once.
static esp_netif_t *s_ap_netif = NULL; //!< Netif for the Access Point

typedef enum {
    WIFI_STATUS_IDLE,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_SUCCESS,
    WIFI_STATUS_FAIL
} wifi_connection_status_t;
static wifi_connection_status_t s_wifi_connection_status = WIFI_STATUS_IDLE;

/* Private Function Prototypes -----------------------------------------------*/
/**
 * @brief register a list of endpoints for the web server
 *
 */
static void register_endpoints(void);
/**
 * @brief Wifi event handler
 *
 * @param arg
 * @param event_base
 * @param event_id
 * @param event_data
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

/**
 * @brief root endpoint handler
 *
 * @param req
 * @return esp_err_t
 */
static esp_err_t on_url_hit(httpd_req_t *req);

/**
 * @brief list of avaliable wifi that device scanned for
 *
 * @param req
 * @return esp_err_t
 */
static esp_err_t Web_get_wifiList(httpd_req_t *req);

/**
 * @brief post handler to parse wifi credentials of the device
 *
 * @param req
 * @return esp_err_t
 */
static esp_err_t Web_post_wificred(httpd_req_t *req);

/**
 * @brief get connection status
 *
 * @param req
 * @return esp_err_t
 */
static esp_err_t Web_get_status(httpd_req_t *req);
/* Public Function Implementation ------------------------------------------*/

/**
 * @brief Initializes the Wi-Fi provisioning module.
 * Scans for available networks and starts an Access Point with a web server.
 */
void WIFI_PROVISIONING_init(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();
    assert(s_ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /** scan for avaliable wifi networks**/
    uint16_t number = 10;
    wifi_ap_record_t ap_info[10];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
    cJSON *ssidArray, *root;
    root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "ssid", ssidArray = cJSON_CreateArray());
    for (int i = 0; (i < 10) && (i < ap_count); i++)
    {
        cJSON_AddItemToArray(ssidArray, cJSON_CreateString((char *)ap_info[i].ssid));
    }
    char *string = cJSON_PrintUnformatted(root);
    memset(s_wifi_json_buffer, 0x00, sizeof(s_wifi_json_buffer));
    strcpy(s_wifi_json_buffer, string);
    cJSON_Delete(root);
    cJSON_free(string);
    /** stop wifi to create an access point **/
    esp_wifi_stop();
    char buffer[25];
    memset(buffer, 0x00, sizeof(buffer));
    char id_buffer[15];
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    sprintf(id_buffer, "%02X%02X%02X", mac[3], mac[4], mac[5]);
    sprintf(buffer, "PROV_%s", id_buffer);
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .max_connection = 15,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = { .required = false },},
    };
    strcpy((char *)wifi_config.ap.ssid, buffer);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(s_ap_netif));

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 10, 0, 10, 1);
    IP4_ADDR(&ip_info.gw, 10, 0, 10, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); // Disable power save for AP mode
    esp_netif_dhcps_start(s_ap_netif);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start the web server now that the AP is up
    register_endpoints();

    ESP_LOGI(TAG, "Initialized");
}

/**
 * @brief Periodic task for the Wi-Fi provisioning module.
 * This function is called repeatedly by the main scheduler.
 */
void WIFI_PROVISIONING_tasks(void)
{
    // This task will be called in a loop.
    // Add your cooperative, non-blocking logic here.
}

/**
 * @brief 1ms tick function for the Wi-Fi provisioning module.
 * This function is called every 1ms from an ISR.
 */
void WIFI_PROVISIONING_tick_1ms(void)
{
    // This function is called every 1ms from an ISR.
    // Keep it short and fast.
}

/* Private Function Implementation -------------------------------------------*/

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

/**
 * @brief root endpoint handler
 *
 * @param req
 * @return esp_err_t
 */
static esp_err_t on_url_hit(httpd_req_t *req)
{
    char path[512 + 16];
    memset(path, 0x00, sizeof(path));

    // If the URI is for the root, serve index.html. Otherwise, construct the path.
    if (strcmp(req->uri, "/") == 0) {
        snprintf(path, sizeof(path), "/spiffs/%s", "index.html");
    } else {
        snprintf(path, sizeof(path), "/spiffs%s", req->uri);
    }

    ESP_LOGI(TAG, "Attempting to serve file: %s", path);

    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", path);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // Set the correct MIME type based on the file extension
    const char *type = "text/plain";
    char *ext = strrchr(path, '.');
    if (ext) {
        if (strcmp(ext, ".html") == 0) type = "text/html";
        else if (strcmp(ext, ".css") == 0) type = "text/css";
        else if (strcmp(ext, ".js") == 0) type = "application/javascript";
        else if (strcmp(ext, ".ico") == 0) type = "image/x-icon";
        else if (strcmp(ext, ".svg") == 0) type = "image/svg+xml";
    }
    httpd_resp_set_type(req, type);
    ESP_LOGI(TAG, "Serving '%s' with MIME type '%s'", path, type);

    char lineRead[256];
    while (fgets(lineRead, sizeof(lineRead), file) != NULL) {
        if (httpd_resp_sendstr_chunk(req, lineRead) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send chunk or client disconnected");
            break; // Exit loop on send error
        }
    }

    httpd_resp_sendstr_chunk(req, NULL);
    fclose(file);
    return ESP_OK;
}

/**
 * @brief list of avaliable wifi that device scanned for
 *
 * @param req
 * @return esp_err_t
 */
static esp_err_t Web_get_wifiList(httpd_req_t *req)
{
    httpd_resp_send(req, s_wifi_json_buffer, strlen(s_wifi_json_buffer));
    return ESP_OK;
}

/**
 * @brief post handler to parse wifi credentials of the device
 *
 * @param req
 *
 * @return esp_err_t
 */
static esp_err_t Web_post_wificred(httpd_req_t *req)
{
    s_wifi_connection_status = WIFI_STATUS_CONNECTING;
    char buf[100];
    memset(&buf, 0, sizeof(buf));
    httpd_req_recv(req, buf, req->content_len);

    // Send an initial OK response to the client
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Data received: %s", buf);
    cJSON *payload = cJSON_Parse(buf);
    cJSON *ssid = cJSON_GetObjectItem(payload, "ssid");
    cJSON *password = cJSON_GetObjectItem(payload, "password");

    // Save credentials
    FLASH_write_to_file("ssid", (char *)ssid->valuestring);
    FLASH_write_to_file("pass", (char *)password->valuestring);

    // Configure and attempt to connect
    if (WIFI_MANAGER_configure_and_connect(ssid->valuestring, password->valuestring) == ESP_OK) {
        s_wifi_connection_status = WIFI_STATUS_SUCCESS;
    } else {
        s_wifi_connection_status = WIFI_STATUS_FAIL;
    }

    cJSON_Delete(payload);
    return ESP_OK;
}

/**
 * @brief register a list of endpoints for the web server
 *
 */
static void register_endpoints(void)
{
    static httpd_handle_t server = NULL;

    if (!s_http_server_started)
    {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.uri_match_fn = httpd_uri_match_wildcard;
        ESP_LOGI(TAG, "Starting server");
        if (httpd_start(&server, &config) != ESP_OK)
        {
            ESP_LOGE(TAG, "COULD NOT START SERVER");
        }
        s_http_server_started = true;
    }

    httpd_uri_t status_uri = {
        .uri       = "/api/status",
        .method    = HTTP_GET,
        .handler   = Web_get_status,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &status_uri);
    httpd_uri_t scanlist = {
        .uri = "/api/scanlist",
        .method = HTTP_GET,
        .handler = Web_get_wifiList};
    httpd_register_uri_handler(server, &scanlist);

    httpd_uri_t Wifi_credentials = {
        .uri = "/api/Wificred",
        .method = HTTP_POST,
        .handler = Web_post_wificred};
    httpd_register_uri_handler(server, &Wifi_credentials);

    httpd_uri_t first_end_point_config = {
        .uri = "/*",
        .method = HTTP_GET, // This will now handle all GET requests
        .handler = on_url_hit};
    httpd_register_uri_handler(server, &first_end_point_config);
}

/**
 * @brief get connection status
 *
 * @param req
 * @return esp_err_t
 */
static esp_err_t Web_get_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "status", s_wifi_connection_status);
    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    cJSON_Delete(root);
    free((void*)json_str);

    // If the status was success or fail, reset it to idle for the next attempt
    if (s_wifi_connection_status == WIFI_STATUS_SUCCESS || s_wifi_connection_status == WIFI_STATUS_FAIL) {
        s_wifi_connection_status = WIFI_STATUS_IDLE;
    }

    return ESP_OK;
}

/* END OF FILE ---------------------------------------------------------------*/
