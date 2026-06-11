/**
 * @file mqtt.c
 * @author Your Name
 * @brief Source file for the MQTT module with full-length descriptive keys.
 */

#include "mqtt.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "wifi_manager.h"
#include "time_management.h"
#include "flash.h"
#include "cJSON.h"
#include <inttypes.h>
#include "system.h"
#include "lte.h"
#include <string.h>
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "mbedtls/base64.h"
#include "light_control.h"

/* Private Defines -----------------------------------------------------------*/
static const char *TAG = "Mqtt";
#define TOPIC_BUF_SIZE 100
#define LWT_TOPIC_SUFFIX "/lwt"                 // Suffix for Last Will and Testament topic
#define LWT_MESSAGE "{\"status\": \"offline\"}" // Last Will message payload
#define LWT_QOS 2
// #define LED_GPIO GPIO_NUM_4
/* Private Variables ---------------------------------------------------------*/
typedef struct
{
    bool tick_1_sec;
    bool tick_10_sec;
    bool tick1min;
    uint32_t counter_1_sec;
    uint32_t heart_beat_1sec_tick;
    uint32_t info_1sec_tick;
    uint32_t heartbeat_interval_sec;
    uint32_t info_interval_sec;
    bool heartbeat_timeout;
    bool info_timeout;
} mqtt_tick_t;

typedef struct
{
    bool client_created;
    bool initial_setup;
    mqtt_tick_t mqtt_tick;
    bool boot_msg;
    bool is_connected;
    int message_count;
    int last_saved_msg_count;
    esp_mqtt_client_handle_t client;
    esp_mqtt_client_config_t mqtt_cfg;
    char light_id[32];
    char lwt_topic[TOPIC_BUF_SIZE]; // Buffer for LWT topic
    char version[32];
    esp_ota_handle_t ota_handle;
    const esp_partition_t *ota_partition;
    bool ota_in_progress;
    uint32_t total_ota_bytes;
} my_mqtt_t;

static my_mqtt_t my_mqtt;

typedef enum
{
    MSG_TYPE_UNKNOWN = 0,
    MSG_TYPE_CONTROL,
    MSG_TYPE_CONFIG,
    MSG_TYPE_OTA_CMD,
    MSG_TYPE_REBOOT
} msg_type_e;

typedef enum
{
    TOPIC_ACTION,
    TOPIC_OTA_CHECK,
    TOPIC_OTA_CHUNKS,
    TOPIC_UNKNOWN
} topic_type_e;

static char s_incoming_payload[8192]; // Buffer for reassembling fragmented MQTT messages
typedef struct
{
    msg_type_e msg_type;
    topic_type_e topic_type;
    int action_id;
    bool msg_received;
} incoming_message_t;

incoming_message_t incoming_message;
/* Private Function Prototypes -----------------------------------------------*/
static void MQTT_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static int MQTT_send_info_message(bool is_boot);
// static int MQTT_send_heartbeat_message(void);
esp_err_t MQTT_increment_message_count(void);
static void MQTT_handle_incoming_message(void);
static void MQTT_handle_ota_check(void);
static void MQTT_handle_ota_chunk(void);
static int MQTT_send_ack_message(int action_id);
static int MQTT_send_ota_ack(int chunk_number, uint32_t crc32);
static int MQTT_send_ota_check_response(bool allowed, const char *reason);
static int MQTT_send_nack_message();
static int MQTT_send_relay_status_message();

/**
 * @brief Compares two version strings (e.g., "1.2.3").
 *
 * @return 1 if latest > running, 0 if equal, -1 if older.
 */
static int compare_versions(const char *latest_version, const char *running_version)
{
    int latest_major = 0, latest_minor = 0, latest_patch = 0;
    int running_major = 0, running_minor = 0, running_patch = 0;

    sscanf(latest_version, "%d.%d.%d", &latest_major, &latest_minor, &latest_patch);
    sscanf(running_version, "%d.%d.%d", &running_major, &running_minor, &running_patch);

    if (latest_major > running_major)
        return 1;
    if (latest_major < running_major)
        return -1;

    if (latest_minor > running_minor)
        return 1;
    if (latest_minor < running_minor)
        return -1;

    if (latest_patch > running_patch)
        return 1;
    if (latest_patch < running_patch)
        return -1;

    return 0;
}

/* Public Function Implementation ------------------------------------------*/
/**
 * @brief mqtt module initialization function. This function initializes the MQTT client configuration, loads persistent data, and prepares the module for operation.
 *
 */
void MQTT_init(void)
{
    memset(&my_mqtt, 0, sizeof(my_mqtt_t));
    const esp_app_desc_t *running_app_info = esp_app_get_description();
    strncpy(my_mqtt.version, running_app_info->version, sizeof(my_mqtt.version));
    my_mqtt.initial_setup = true;
    my_mqtt.boot_msg = true;
    if (FLASH_file_exists("config"))
    {
        cJSON *config_json = NULL;
        char config_buffer[256];
        FLASH_read_file("config", config_buffer, sizeof(config_buffer));
        config_json = cJSON_Parse(config_buffer);
        if (config_json != NULL)
        {
            cJSON *hb_interval = cJSON_GetObjectItem(config_json, "hb_interval");
            cJSON *light_id = cJSON_GetObjectItem(config_json, "light_id");
            cJSON *info_interval = cJSON_GetObjectItem(config_json, "info_interval");
            my_mqtt.mqtt_tick.heartbeat_interval_sec = hb_interval->valueint;
            strncpy(my_mqtt.light_id, light_id->valuestring, sizeof(my_mqtt.light_id));
            my_mqtt.mqtt_tick.info_interval_sec = info_interval->valueint;
        }
    }
    else
    {
        my_mqtt.mqtt_tick.heartbeat_interval_sec = 60; // Default to 60 seconds if not set
        my_mqtt.mqtt_tick.info_interval_sec = 75;      // Default to 75 seconds if not set
        strncpy(my_mqtt.light_id, "db1", sizeof(my_mqtt.light_id));

        // If config file is not found, initialize with both MQTT and Light Control defaults
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "hb_interval", my_mqtt.mqtt_tick.heartbeat_interval_sec);
        cJSON_AddStringToObject(root, "light_id", my_mqtt.light_id);
        cJSON_AddNumberToObject(root, "info_interval", my_mqtt.mqtt_tick.info_interval_sec);

        int default_system_type;
        char default_relay_names[3][32];
        LIGHT_CONTROL_get_default_settings(&default_system_type, default_relay_names);
        cJSON_AddNumberToObject(root, "system_type", default_system_type);
        cJSON *relay_names_array = cJSON_CreateArray();
        for (int i = 0; i < default_system_type; i++)
        {
            cJSON_AddItemToArray(relay_names_array, cJSON_CreateString(default_relay_names[i]));
        }
        cJSON_AddItemToObject(root, "relay_names", relay_names_array);
        char *json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (json_string)
        {
            FLASH_write_to_file("config", json_string);
            free(json_string);
            ESP_LOGI(TAG, "Initial config created with defaults.");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to create initial config JSON string.");
        }
    }

    my_mqtt.is_connected = false;
    incoming_message.msg_received = false;
    my_mqtt.mqtt_tick.heartbeat_timeout = false;

    // Load persistent message count
    char count_buf[16] = {0};
    if (FLASH_read_file("msg_count", count_buf, sizeof(count_buf)) == ESP_OK)
    {
        my_mqtt.message_count = atoi(count_buf);
        my_mqtt.last_saved_msg_count = my_mqtt.message_count;
    }

    // Safely load or default the gate/topic name
    ESP_LOGI(TAG, "Initialized Gate ID: %s", my_mqtt.light_id);

    snprintf(my_mqtt.lwt_topic, sizeof(my_mqtt.lwt_topic), "lighting/control/%s%s", my_mqtt.light_id, LWT_TOPIC_SUFFIX);
    my_mqtt.mqtt_cfg.session.last_will.topic = my_mqtt.lwt_topic;
    my_mqtt.mqtt_cfg.session.last_will.msg = LWT_MESSAGE;
    my_mqtt.mqtt_cfg.session.last_will.qos = LWT_QOS;
    my_mqtt.mqtt_cfg.session.last_will.retain = 0;
}
/**
 * @brief mqtt tasks function. This function should be called periodically from the main loop to handle MQTT connection management, message dispatching, and other time-sensitive operations.
 *
 */
void MQTT_tasks(void)
{
    if (!WIFI_MANAGER_is_configured() && !LTE_is_ready())
        return;

    // 1. Connection Management
    if (my_mqtt.mqtt_tick.tick_10_sec || my_mqtt.initial_setup)
    {
        my_mqtt.mqtt_tick.tick_10_sec = false;
        my_mqtt.initial_setup = false;

        if (System_is_connected() && !my_mqtt.client_created)
        {
            my_mqtt.mqtt_cfg.broker.address.uri = "mqtts://hf40a227.ala.eu-central-1.emqxsl.com:8883";
            // Note: If my_mqtt.light_id changes after initial setup, the LWT topic will only update
            // on the next device reboot, as esp_mqtt_client_init is called only once here.
            // For dynamic LWT topic updates during runtime, the MQTT client would need to be
            // deinitialized and reinitialized with the updated configuration.
            // The lwt_topic is set in MQTT_init based on the initial my_mqtt.light_id.
            my_mqtt.mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
            my_mqtt.mqtt_cfg.credentials.username = "Court1";
            my_mqtt.mqtt_cfg.credentials.authentication.password = "1234";
            my_mqtt.mqtt_cfg.credentials.client_id = my_mqtt.light_id;
            my_mqtt.mqtt_cfg.session.keepalive = 60;

            my_mqtt.client = esp_mqtt_client_init(&my_mqtt.mqtt_cfg);
            if (my_mqtt.client != NULL)
            {
                esp_mqtt_client_register_event(my_mqtt.client, MQTT_EVENT_ANY, MQTT_event_handler, NULL);
                esp_mqtt_client_start(my_mqtt.client);
                my_mqtt.client_created = true;
                ESP_LOGI(TAG, "Starting MQTT Client...");
            }
        }
        if ((my_mqtt.is_connected == false) && (System_is_connected() == true) && my_mqtt.client_created == true)
        {
            ESP_LOGI(TAG, "Link is up but MQTT is down. Requesting reconnect...");
            esp_mqtt_client_start(my_mqtt.client); // Use start to refresh the whole transport state
            esp_mqtt_client_reconnect(my_mqtt.client);
        }
    }

    // 2. Message Dispatching
    if (my_mqtt.is_connected)
    {
        // Handle Boot Event
        if (my_mqtt.boot_msg)
        {
            if (MQTT_send_info_message(true) > -1)
            {
                my_mqtt.boot_msg = false;
                MQTT_send_relay_status_message(); // Send initial relay status after boot
            }
        }
        if (incoming_message.msg_received)
        {
            incoming_message.msg_received = false;
            if (incoming_message.topic_type == TOPIC_OTA_CHUNKS)
            {
                MQTT_handle_ota_chunk();
            }
            else if (incoming_message.topic_type == TOPIC_OTA_CHECK)
            {
                MQTT_handle_ota_check();
            }
            else
            {
                MQTT_handle_incoming_message();
            }
        }
        // Handle Heartbeat Interval
        // if (my_mqtt.mqtt_tick.heartbeat_timeout)
        // {
        //     if (MQTT_send_heartbeat_message() > -1)
        //     {
        //         my_mqtt.mqtt_tick.heartbeat_timeout = false;
        //     }
        // }
        // Handle Info Interval
        if (my_mqtt.mqtt_tick.info_timeout)
        {
            if (MQTT_send_info_message(false) > -1)
            {
                my_mqtt.mqtt_tick.info_timeout = false;
            }
        }
    }

    // Optimized msg_count persistence: Only if connected AND value changed
    if (my_mqtt.mqtt_tick.tick1min && my_mqtt.is_connected && (my_mqtt.message_count != my_mqtt.last_saved_msg_count))
    {
        my_mqtt.mqtt_tick.tick1min = false;
        MQTT_increment_message_count();
    }

    my_mqtt.mqtt_tick.tick_1_sec = false;
}
/**
 * @brief mqtt tick function. This function is designed to be called from a timer ISR every second. It updates internal counters and flags used for timing MQTT operations such as heartbeats.
 *
 */
void MQTT_tick_1sec(void)
{
    my_mqtt.mqtt_tick.tick_1_sec = true;
    my_mqtt.mqtt_tick.counter_1_sec++;

    if (my_mqtt.mqtt_tick.counter_1_sec % 10 == 0)
    {
        my_mqtt.mqtt_tick.tick_10_sec = true;
        my_mqtt.mqtt_tick.counter_1_sec = 0;
    }

    if (my_mqtt.mqtt_tick.heart_beat_1sec_tick++ >= my_mqtt.mqtt_tick.heartbeat_interval_sec)
    {
        my_mqtt.mqtt_tick.heart_beat_1sec_tick = 0;
        my_mqtt.mqtt_tick.heartbeat_timeout = true;
    }

    if (my_mqtt.mqtt_tick.info_1sec_tick++ >= my_mqtt.mqtt_tick.info_interval_sec)
    {
        my_mqtt.mqtt_tick.info_1sec_tick = 0;
        my_mqtt.mqtt_tick.info_timeout = true;
    }
}

void MQTT_tick_1min(void)
{
    my_mqtt.mqtt_tick.tick1min = true;
}

/* Private Function Implementation -----------------------------------------*/
// Implement private functions here

/* @brief MQTT event handler callback function. This will be called by the MQTT client when events occur.
 *
 * @param event_handler_arg
 * @param event_base
 * @param event_id
 * @param event_data
 * @return esp_event_handler_t
 */
void MQTT_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    static char sub_topic[TOPIC_BUF_SIZE];
    static char check_topic[TOPIC_BUF_SIZE];
    static char ota_topic[TOPIC_BUF_SIZE];
    snprintf(sub_topic, sizeof(sub_topic), "lighting/control/%s/action", my_mqtt.light_id);
    snprintf(check_topic, sizeof(check_topic), "lighting/control/%s/ota/check", my_mqtt.light_id);
    snprintf(ota_topic, sizeof(ota_topic), "lighting/control/%s/ota/firmware/chunks", my_mqtt.light_id);

    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        my_mqtt.is_connected = true;
        esp_mqtt_client_subscribe(my_mqtt.client, sub_topic, 1);
        esp_mqtt_client_subscribe(my_mqtt.client, check_topic, 1);
        esp_mqtt_client_subscribe(my_mqtt.client, ota_topic, 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        my_mqtt.is_connected = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        my_mqtt.message_count++; // Just increment in RAM
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG, "MQTT_EVENT_DATA: offset=%d, len=%d, total=%d",
                 event->current_data_offset, event->data_len, event->total_data_len);

        // 1. Differentiate topic type ONLY on the first fragment (offset 0)
        if (event->current_data_offset == 0)
        {
            if (event->topic && event->topic_len >= 6 &&
                strncmp(event->topic + event->topic_len - 6, "chunks", 6) == 0)
            {
                incoming_message.topic_type = TOPIC_OTA_CHUNKS;
            }
            else if (event->topic && event->topic_len >= 5 &&
                     strncmp(event->topic + event->topic_len - 5, "check", 5) == 0)
            {
                incoming_message.topic_type = TOPIC_OTA_CHECK;
            }
            else
            {
                incoming_message.topic_type = TOPIC_ACTION;
            }
        }

        // 2. Safety check: avoid buffer overflow AND don't overwrite if main task is busy
        if (!incoming_message.msg_received &&
            (event->current_data_offset + event->data_len <= sizeof(s_incoming_payload) - 1))
        {
            // 3. Reassemble fragments into the static buffer
            memcpy(s_incoming_payload + event->current_data_offset, event->data, event->data_len);
        }
        else if (incoming_message.msg_received)
        {
            ESP_LOGW(TAG, "Main task busy, dropping packet to prevent corruption");
        }

        // 4. Only signal parsing when the entire message has arrived
        if (event->current_data_offset + event->data_len == event->total_data_len)
        {
            s_incoming_payload[event->total_data_len] = '\0';
            incoming_message.msg_received = true;
            ESP_LOGD(TAG, "Full message reassembled (%d bytes)", event->total_data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%" PRIi32, event_id);
        break;
    }
}

/**
 * @brief send boot message to MQTT broker when the client connects for the first time.
 *
 */
static int MQTT_send_info_message(bool is_boot)
{
    // This function can be called to send a boot message to the MQTT broker.
    // It can be called from the event handler when the client connects for the first time.
    char topic[100];
    int result = -1;
    memset(topic, 0, sizeof(topic));
    snprintf(topic, sizeof(topic), "lighting/control/%s/info", my_mqtt.light_id);
    if (my_mqtt.is_connected)
    {
        cJSON *root = cJSON_CreateObject();
        is_boot ? cJSON_AddStringToObject(root, "msgType", "boot") : cJSON_AddStringToObject(root, "msgType", "info");
        cJSON_AddNumberToObject(root, "seqID", my_mqtt.message_count);
        cJSON_AddNumberToObject(root, "timestamp", TIME_MANAGEMENT_get_epoch_time());
        cJSON_AddNumberToObject(root, "uptime_mins", TIME_MANAGEMENT_get_runtime_in_minutes());
        cJSON_AddStringToObject(root, "version", my_mqtt.version);
        cJSON_AddNumberToObject(root, "rssi", LTE_get_rssi());
        cJSON_AddNumberToObject(root, "systemType", LIGHT_CONTROL_get_system_type());
        char *json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        result = esp_mqtt_client_publish(my_mqtt.client, topic, json_string, 0, 1, 0);
        free(json_string);
        if (is_boot)
        {
            ESP_LOGI(TAG, "Boot message sent");
        }
        else
        {
            ESP_LOGI(TAG, "Info message sent");
        }
    }
    return result;
}

// /**
//  * @brief send heartbeat message to MQTT broker
//  *
//  */
// static int MQTT_send_heartbeat_message(void)
// {
//     char topic[100];
//     int result = -1;
//     memset(topic, 0, sizeof(topic));
//     snprintf(topic, sizeof(topic), "lighting/control/%s/heartbeat", my_mqtt.light_id);
//     if (my_mqtt.is_connected)
//     {
//         cJSON *root = cJSON_CreateObject();
//         cJSON_AddStringToObject(root, "msgType", "heartbeat");
//         cJSON_AddNumberToObject(root, "seqID", my_mqtt.message_count);
//         cJSON_AddNumberToObject(root, "timestamp", TIME_MANAGEMENT_get_epoch_time());
//         cJSON_AddNumberToObject(root, "uptime_mins", TIME_MANAGEMENT_get_runtime_in_minutes());
//         char *json_string = cJSON_PrintUnformatted(root);
//         cJSON_Delete(root);
//         result = esp_mqtt_client_publish(my_mqtt.client, topic, json_string, 0, 1, 0);
//         free(json_string);
//         ESP_LOGI(TAG, "heartbeat message sent");
//     }
//     return result;
// }
/**
 * @brief  Function to increment the message count and save it to flash.
 * This can be called whenever a message is published successfully.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t MQTT_increment_message_count(void)
{
    char count_buf[16];
    snprintf(count_buf, sizeof(count_buf), "%d", my_mqtt.message_count);
    esp_err_t err = FLASH_write_to_file("msg_count", count_buf);
    if (err == ESP_OK)
    {
        my_mqtt.last_saved_msg_count = my_mqtt.message_count;
    }
    return err;
}

static void MQTT_handle_incoming_message(void)
{
    // 1. Safety check: Ensure payload isn't empty or just whitespace
    if (s_incoming_payload[0] == '\0')
    {
        ESP_LOGE(TAG, "Empty payload received");
        MQTT_send_nack_message();
        return;
    }

    ESP_LOGI(TAG, "Parsing incoming message...");

    // 2. Attempt to parse JSON
    cJSON *root = cJSON_Parse(s_incoming_payload);
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Invalid JSON format received");
        MQTT_send_nack_message();
        return;
    }

    // Reset local message state
    incoming_message.msg_type = MSG_TYPE_UNKNOWN;
    incoming_message.action_id = -1;

    // 3. Extract Core Fields
    cJSON *msgType = cJSON_GetObjectItem(root, "msgType");
    cJSON *actionID = cJSON_GetObjectItem(root, "actionID");

    if (actionID != NULL && cJSON_IsNumber(actionID))
    {
        incoming_message.action_id = actionID->valueint;
    }

    if (msgType != NULL && cJSON_IsString(msgType))
    {
        if (strcmp(msgType->valuestring, "control") == 0)
        {
            incoming_message.msg_type = MSG_TYPE_CONTROL;
        }
        else if (strcmp(msgType->valuestring, "config") == 0)
        {
            incoming_message.msg_type = MSG_TYPE_CONFIG;
        }
        else if (strcmp(msgType->valuestring, "ota") == 0)
        {
            incoming_message.msg_type = MSG_TYPE_OTA_CMD;
        }
        else if (strcmp(msgType->valuestring, "reboot") == 0)
        {
            incoming_message.msg_type = MSG_TYPE_REBOOT;
        }
    }

    // 4. Processing Logic
    if (incoming_message.msg_type == MSG_TYPE_CONTROL)
    {
        cJSON *relay_idx = cJSON_GetObjectItem(root, "relay");
        cJSON *state = cJSON_GetObjectItem(root, "state");

        if (cJSON_IsNumber(relay_idx) && cJSON_IsBool(state))
        {
            LIGHT_CONTROL_set_relay(relay_idx->valueint, cJSON_IsTrue(state));
            MQTT_send_ack_message(incoming_message.action_id);
            MQTT_send_relay_status_message();
        }
        else
        {
            MQTT_send_nack_message();
        }
    }
    else if (incoming_message.msg_type == MSG_TYPE_CONFIG)
    {
        ESP_LOGI(TAG, "Received CONFIG message with actionID: %d", incoming_message.action_id);

        // 1. MQTT Specific intervals and IDs
        cJSON *heartbeat = cJSON_GetObjectItem(root, "hb_interval");
        if (cJSON_IsNumber(heartbeat))
        {
            my_mqtt.mqtt_tick.heartbeat_interval_sec = heartbeat->valueint;
            ESP_LOGI(TAG, "Updated heartbeat interval to %d seconds", (int)my_mqtt.mqtt_tick.heartbeat_interval_sec);
        }

        cJSON *info = cJSON_GetObjectItem(root, "info_interval");
        if (cJSON_IsNumber(info))
        {
            my_mqtt.mqtt_tick.info_interval_sec = info->valueint;
            ESP_LOGI(TAG, "Updated info interval to %d seconds", (int)my_mqtt.mqtt_tick.info_interval_sec);
        }

        cJSON *light_id_json = cJSON_GetObjectItem(root, "light_id");
        if (cJSON_IsString(light_id_json))
        {
            strncpy(my_mqtt.light_id, light_id_json->valuestring, sizeof(my_mqtt.light_id));
            ESP_LOGI(TAG, "Updated gate ID to %s", my_mqtt.light_id);
        }

        cJSON *system_type_json = cJSON_GetObjectItem(root, "system_type");
        cJSON *relay_names_array = cJSON_GetObjectItem(root, "relay_names");
        if (cJSON_IsNumber(system_type_json) && cJSON_IsArray(relay_names_array))
        {
            int new_system_type = system_type_json->valueint;
            char name1[32] = {0}, name2[32] = {0}, name3[32] = {0};
            if (cJSON_GetArraySize(relay_names_array) > 0 && cJSON_IsString(cJSON_GetArrayItem(relay_names_array, 0)))
                strncpy(name1, cJSON_GetArrayItem(relay_names_array, 0)->valuestring, sizeof(name1) - 1);
            if (cJSON_GetArraySize(relay_names_array) > 1 && cJSON_IsString(cJSON_GetArrayItem(relay_names_array, 1)))
                strncpy(name2, cJSON_GetArrayItem(relay_names_array, 1)->valuestring, sizeof(name2) - 1);
            if (cJSON_GetArraySize(relay_names_array) > 2 && cJSON_IsString(cJSON_GetArrayItem(relay_names_array, 2)))
                strncpy(name3, cJSON_GetArrayItem(relay_names_array, 2)->valuestring, sizeof(name3) - 1);

            LIGHT_CONTROL_set_system_config(new_system_type, name1, name2, name3);
            MQTT_send_relay_status_message();
        }

        // Save all changes and acknowledge
        MQTT_Update_config_save_to_flash();
        MQTT_send_ack_message(incoming_message.action_id);
    }
    else if (incoming_message.msg_type == MSG_TYPE_OTA_CMD)
    {
        ESP_LOGI(TAG, "Received OTA message with actionID: %d", incoming_message.action_id);
        // Handle OTA message based on action_id
        MQTT_send_ack_message(incoming_message.action_id);
        OTA_force_check_for_update();
    }
    else if (incoming_message.msg_type == MSG_TYPE_REBOOT)
    {
        ESP_LOGI(TAG, "Received REBOOT message with actionID: %d", incoming_message.action_id);
        MQTT_send_ack_message(incoming_message.action_id);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay to allow MQTT to finish sending the ACK
        esp_restart();
    }
    else
    {
        ESP_LOGW(TAG, "Unknown message type or missing parameters");
        MQTT_send_nack_message();
    }

    cJSON_Delete(root);
}

/**
 * @brief Handles the OTA version check handshake.
 */
static void MQTT_handle_ota_check(void)
{
    cJSON *root = cJSON_Parse(s_incoming_payload);
    if (root == NULL)
        return;

    cJSON *version_json = cJSON_GetObjectItem(root, "version");
    if (!cJSON_IsString(version_json) || (version_json->valuestring == NULL))
    {
        ESP_LOGE(TAG, "Version field missing in ota_check");
        MQTT_send_ota_check_response(false, "Missing version field");
        cJSON_Delete(root);
        return;
    }

    const char *dashboard_version = version_json->valuestring;
    const char *current_version = my_mqtt.version;
    bool allowed = false;
    char reason[128];

    int cmp = compare_versions(dashboard_version, current_version);

    if (cmp > 0)
    {
        allowed = true;
        snprintf(reason, sizeof(reason), "Upgrade from %s to %s available", current_version, dashboard_version);
        ESP_LOGI(TAG, "OTA Check: %s. Allowed.", reason);
    }
    else if (cmp == 0)
    {
        allowed = false;
        snprintf(reason, sizeof(reason), "Already running version %s", current_version);
        ESP_LOGI(TAG, "OTA Check: %s. Rejected.", reason);
    }
    else
    {
        allowed = false;
        snprintf(reason, sizeof(reason), "Dashboard version %s is older than %s", dashboard_version, current_version);
        ESP_LOGW(TAG, "OTA Check: %s. Rejected.", reason);
    }

    MQTT_send_ota_check_response(allowed, reason);
    cJSON_Delete(root);
}

/**
 * @brief Process an incoming OTA firmware chunk received via MQTT.
 */
static void MQTT_handle_ota_chunk(void)
{
    // ESP_LOGI(TAG, "Incoming OTA Chunk JSON: %s", s_incoming_payload);
    cJSON *root = cJSON_Parse(s_incoming_payload);
    if (root == NULL)
        return;

    cJSON *chunk_num_json = cJSON_GetObjectItem(root, "chunk_number");
    cJSON *data_json = cJSON_GetObjectItem(root, "data");
    cJSON *crc_json = cJSON_GetObjectItem(root, "crc32");
    cJSON *is_final_json = cJSON_GetObjectItem(root, "is_final");

    if (!chunk_num_json || !data_json || !crc_json)
    {
        ESP_LOGE(TAG, "Malformed OTA chunk payload");
        cJSON_Delete(root);
        return;
    }

    int chunk_num = chunk_num_json->valueint;
    const char *b64_data = data_json->valuestring;
    uint32_t received_crc = (uint32_t)crc_json->valuedouble;

    // 1. Initial Start logic
    if (chunk_num == 1 && !my_mqtt.ota_in_progress)
    {
        my_mqtt.ota_partition = esp_ota_get_next_update_partition(NULL);
        if (my_mqtt.ota_partition == NULL)
        {
            ESP_LOGE(TAG, "Passive OTA partition not found");
            cJSON_Delete(root);
            return;
        }
        esp_err_t err = esp_ota_begin(my_mqtt.ota_partition, OTA_SIZE_UNKNOWN, &my_mqtt.ota_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
            cJSON_Delete(root);
            return;
        }
        my_mqtt.ota_in_progress = true;
        my_mqtt.total_ota_bytes = 0;
        ESP_LOGI(TAG, "MQTT OTA session started on partition %s", my_mqtt.ota_partition->label);
    }

    if (!my_mqtt.ota_in_progress)
    {
        cJSON_Delete(root);
        return;
    }

    // 2. Decode Base64 data string to raw binary
    size_t b64_len = strlen(b64_data);
    size_t decoded_len = (b64_len * 3) / 4 + 2; // Dynamic buffer calculation
    uint8_t *decoded_buf = malloc(decoded_len);

    if (decoded_buf == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for OTA chunk decoding");
        cJSON_Delete(root);
        return;
    }

    int ret = mbedtls_base64_decode(decoded_buf, decoded_len, &decoded_len, (const unsigned char *)b64_data, b64_len);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Base64 decode failed for chunk %d", chunk_num);
        free(decoded_buf);
        cJSON_Delete(root);
        return;
    }

    // 3. Integrity Check (CRC32 IEEE 802.3)
    uint32_t calculated_crc = esp_rom_crc32_le(0, decoded_buf, decoded_len);
    if (calculated_crc != received_crc)
    {
        ESP_LOGE(TAG, "CRC Mismatch on chunk %d! Calc: %lu, Recv: %lu", chunk_num, (unsigned long)calculated_crc, (unsigned long)received_crc);
        free(decoded_buf);
        cJSON_Delete(root);
        return;
    }

    // 4. Write binary data to the OTA storage partition
    esp_err_t err = esp_ota_write(my_mqtt.ota_handle, decoded_buf, decoded_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        free(decoded_buf);
        cJSON_Delete(root);
        return;
    }
    my_mqtt.total_ota_bytes += decoded_len;

    // 5. Send mandatory ACK
    MQTT_send_ota_ack(chunk_num, calculated_crc);

    free(decoded_buf);

    // 6. Handle Finalization
    if (is_final_json && cJSON_IsTrue(is_final_json))
    {
        ESP_LOGI(TAG, "Final chunk acknowledged. Total bytes: %lu. Finalizing...", my_mqtt.total_ota_bytes);
        if (esp_ota_end(my_mqtt.ota_handle) == ESP_OK)
        {
            if (esp_ota_set_boot_partition(my_mqtt.ota_partition) == ESP_OK)
            {
                ESP_LOGW(TAG, "OTA partition set as boot. Restarting system...");
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            }
        }
        my_mqtt.ota_in_progress = false;
    }

    cJSON_Delete(root);
}

/**
 * @brief updates the heartbeat interval in flash memory. This function can be called whenever the heartbeat interval is changed to ensure that the new value is persisted across reboots.
 *
 */
void MQTT_Update_config_save_to_flash()
{
    static char sub_topic[TOPIC_BUF_SIZE];
    static char check_topic[TOPIC_BUF_SIZE];
    static char ota_topic[TOPIC_BUF_SIZE];

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "hb_interval", my_mqtt.mqtt_tick.heartbeat_interval_sec);
    cJSON_AddStringToObject(root, "light_id", my_mqtt.light_id);
    cJSON_AddNumberToObject(root, "info_interval", my_mqtt.mqtt_tick.info_interval_sec);

    // Include Light Control settings in the unified config
    int sys_type = LIGHT_CONTROL_get_system_type();
    cJSON_AddNumberToObject(root, "system_type", sys_type);
    cJSON *relay_names = cJSON_CreateArray();
    for (int i = 0; i < sys_type && i < 3; i++)
    {
        cJSON_AddItemToArray(relay_names, cJSON_CreateString(LIGHT_CONTROL_get_relay_name(i)));
    }
    cJSON_AddItemToObject(root, "relay_names", relay_names);

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    FLASH_write_to_file("config", json_string);
    free(json_string);
    ESP_LOGI(TAG, "Config updated and saved to flash");
    if (my_mqtt.is_connected)
    {
        esp_mqtt_client_unsubscribe(my_mqtt.client, "lighting/control/+/action");
        esp_mqtt_client_unsubscribe(my_mqtt.client, "lighting/control/+/ota/check");
        esp_mqtt_client_unsubscribe(my_mqtt.client, "lighting/control/+/ota/firmware/chunks");
        snprintf(sub_topic, sizeof(sub_topic), "lighting/control/%s/action", my_mqtt.light_id);
        snprintf(check_topic, sizeof(check_topic), "lighting/control/%s/ota/check", my_mqtt.light_id);
        snprintf(ota_topic, sizeof(ota_topic), "lighting/control/%s/ota/firmware/chunks", my_mqtt.light_id);
        esp_mqtt_client_subscribe(my_mqtt.client, sub_topic, 1);
        esp_mqtt_client_subscribe(my_mqtt.client, check_topic, 1);
        esp_mqtt_client_subscribe(my_mqtt.client, ota_topic, 1);
    }
}

/**
 * @brief send ack message to MQTT broker
 *
 */
static int MQTT_send_ack_message(int action_id)
{
    char topic[100];
    int result = -1;
    memset(topic, 0, sizeof(topic));
    snprintf(topic, sizeof(topic), "lighting/control/%s/status", my_mqtt.light_id);
    if (my_mqtt.is_connected)
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "msgType", "ack");
        cJSON_AddNumberToObject(root, "seqID", my_mqtt.message_count);
        cJSON_AddNumberToObject(root, "timestamp", TIME_MANAGEMENT_get_epoch_time());
        cJSON_AddNumberToObject(root, "actionID", action_id);
        char *json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        result = esp_mqtt_client_publish(my_mqtt.client, topic, json_string, 0, 1, 0);
        free(json_string);
        ESP_LOGI(TAG, "ack message sent");
    }
    return result;
}

/**
 * @brief Sends an acknowledgment for an OTA firmware chunk.
 */
static int MQTT_send_ota_ack(int chunk_number, uint32_t crc32)
{
    char topic[TOPIC_BUF_SIZE];
    int result = -1;
    snprintf(topic, sizeof(topic), "lighting/control/%s/ota/ack", my_mqtt.light_id);
    if (my_mqtt.is_connected)
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "msgType", "ack");
        cJSON_AddNumberToObject(root, "chunk_number", chunk_number);
        cJSON_AddNumberToObject(root, "crc32", crc32);
        char *json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        result = esp_mqtt_client_publish(my_mqtt.client, topic, json_string, 0, 1, 0);
        free(json_string);
        ESP_LOGI(TAG, "OTA chunk %d ACK sent", chunk_number);
    }
    return result;
}

/**
 * @brief Sends the response for the OTA version check.
 */
static int MQTT_send_ota_check_response(bool allowed, const char *reason)
{
    char topic[TOPIC_BUF_SIZE];
    int result = -1;
    snprintf(topic, sizeof(topic), "lighting/control/%s/ota/ack", my_mqtt.light_id);

    if (my_mqtt.is_connected)
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "msgType", "ota_check_response");
        cJSON_AddBoolToObject(root, "allowed", allowed);
        cJSON_AddStringToObject(root, "reason", reason);
        char *json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        result = esp_mqtt_client_publish(my_mqtt.client, topic, json_string, 0, 1, 0);
        free(json_string);
    }
    return result;
}

/**
 * @brief send nack message to MQTT broker
 *
 */
static int MQTT_send_nack_message()
{
    char topic[100];
    int result = -1;
    memset(topic, 0, sizeof(topic));
    snprintf(topic, sizeof(topic), "lighting/control/%s/status", my_mqtt.light_id);
    if (my_mqtt.is_connected)
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "msgType", "nack");
        cJSON_AddNumberToObject(root, "seqID", my_mqtt.message_count);
        cJSON_AddNumberToObject(root, "timestamp", TIME_MANAGEMENT_get_epoch_time());
        char *json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        result = esp_mqtt_client_publish(my_mqtt.client, topic, json_string, 0, 1, 0);
        free(json_string);
        ESP_LOGI(TAG, "nack message sent");
    }
    return result;
}

/**
 * @brief send current relay status to MQTT broker
 *
 */
static int MQTT_send_relay_status_message()
{
    char topic[100];
    int result = -1;
    memset(topic, 0, sizeof(topic));
    snprintf(topic, sizeof(topic), "lighting/control/%s/relay", my_mqtt.light_id);
    if (my_mqtt.is_connected)
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "msgType", "relay_status");
        cJSON_AddNumberToObject(root, "seqID", my_mqtt.message_count);
        cJSON_AddNumberToObject(root, "timestamp", TIME_MANAGEMENT_get_epoch_time() / 1000); // Unix epoch in seconds

        cJSON *relays = cJSON_CreateArray();
        int system_type = LIGHT_CONTROL_get_system_type();

        for (int i = 0; i < system_type; i++)
        { // Loop up to the configured system_type
            cJSON *relay_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(relay_obj, "name", LIGHT_CONTROL_get_relay_name(i));
            cJSON_AddBoolToObject(relay_obj, "state", LIGHT_CONTROL_get_relay_state(i)); // Use s_relay_states
            cJSON_AddItemToArray(relays, relay_obj);
        }
        cJSON_AddItemToObject(root, "relays", relays);

        char *json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        result = esp_mqtt_client_publish(my_mqtt.client, topic, json_string, 0, 1, 0);
        free(json_string);
        ESP_LOGI(TAG, "Relay status message sent");
    }
    return result;
}