/**
 * @file light_control.c
 * @author Your Name
 * @brief Source file for the light_control module.
 * @version 0.1
 * @date 2026-06-02
 * @copyright Copyright (c) 2026
 */

/* Includes ------------------------------------------------------------------*/
#include "light_control.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "flash.h"
#include "mqtt.h" // Required for MQTT_Update_config_save_to_flash
#include <string.h>

/* Private Defines -----------------------------------------------------------*/
static const char *TAG = "Light Control";

/**
 * RELAY_1_GPIO is set to 3 as per PCB layout.
 * This pin may experience brief transients during early boot, potentially causing the relay to click.
 * The software attempts to configure it as output low as early as possible.
 */
#define RELAY_1_GPIO 3
#define RELAY_2_GPIO 6
#define RELAY_3_GPIO 7

static const int relay_gpios[3] = {RELAY_1_GPIO, RELAY_2_GPIO, RELAY_3_GPIO};

/* Private Variables ---------------------------------------------------------*/
typedef struct {
    int system_type;
    char relay_names[3][32]; // Assuming max 3 relays, 32 char name
} light_config_t;

static light_config_t s_light_config;
static bool s_relay_states[3] = {false, false, false};

/* Private Function Prototypes -----------------------------------------------*/
static void LIGHT_CONTROL_save_config_to_flash(void);

/* Public Function Implementation ------------------------------------------*/

void LIGHT_CONTROL_init(void)
{
    // Load configuration from flash
    cJSON *config_json = NULL;
    char config_buffer[256]; // Buffer for reading config from flash

    if (FLASH_read_file("config", config_buffer, sizeof(config_buffer)) == ESP_OK) {
        config_json = cJSON_Parse(config_buffer);
        if (config_json != NULL) {
            cJSON *system_type_json = cJSON_GetObjectItem(config_json, "system_type");
            if (cJSON_IsNumber(system_type_json)) {
                s_light_config.system_type = system_type_json->valueint;
            } else {
                s_light_config.system_type = 3; // Default if not found
            }

            cJSON *relay_names_array = cJSON_GetObjectItem(config_json, "relay_names");
            if (cJSON_IsArray(relay_names_array)) {
                for (int i = 0; i < s_light_config.system_type && i < 3; i++) {
                    cJSON *name_item = cJSON_GetArrayItem(relay_names_array, i);
                    if (cJSON_IsString(name_item)) {
                        strncpy(s_light_config.relay_names[i], name_item->valuestring, sizeof(s_light_config.relay_names[i]) - 1);
                        s_light_config.relay_names[i][sizeof(s_light_config.relay_names[i]) - 1] = '\0';
                    }
                }
            }
            cJSON_Delete(config_json);
        } else {
            ESP_LOGE(TAG, "Failed to parse light_config from flash, using defaults.");
            s_light_config.system_type = 3; // Default
            strncpy(s_light_config.relay_names[0], "gate1", sizeof(s_light_config.relay_names[0]) - 1);
            strncpy(s_light_config.relay_names[1], "gate2", sizeof(s_light_config.relay_names[1]) - 1);
            strncpy(s_light_config.relay_names[2], "patio", sizeof(s_light_config.relay_names[2]) - 1);
            LIGHT_CONTROL_save_config_to_flash();
        }
    } else {
        ESP_LOGI(TAG, "light_config not found in flash, using defaults.");
        s_light_config.system_type = 3; // Default
        strncpy(s_light_config.relay_names[0], "gate1", sizeof(s_light_config.relay_names[0]) - 1);
        strncpy(s_light_config.relay_names[1], "gate2", sizeof(s_light_config.relay_names[1]) - 1);
        strncpy(s_light_config.relay_names[2], "patio", sizeof(s_light_config.relay_names[2]) - 1);
        LIGHT_CONTROL_save_config_to_flash();
    }

    for (int i = 0; i < s_light_config.system_type; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << relay_gpios[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,   // Ensure no pull-up
            .pull_down_en = GPIO_PULLDOWN_DISABLE, // Ensure no pull-down
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level(relay_gpios[i], 0); // Explicitly set low
        s_relay_states[i] = false;
    }
    ESP_LOGI(TAG, "Initialized %d-relay system", s_light_config.system_type);
}

void LIGHT_CONTROL_tasks(void) { }

void LIGHT_CONTROL_set_system_config(int type, const char* name1, const char* name2, const char* name3)
{
    s_light_config.system_type = type;
    strncpy(s_light_config.relay_names[0], name1, sizeof(s_light_config.relay_names[0]) - 1);
    s_light_config.relay_names[0][sizeof(s_light_config.relay_names[0]) - 1] = '\0';
    if (type > 1) {
        strncpy(s_light_config.relay_names[1], name2, sizeof(s_light_config.relay_names[1]) - 1);
        s_light_config.relay_names[1][sizeof(s_light_config.relay_names[1]) - 1] = '\0';
    }
    if (type > 2) {
        strncpy(s_light_config.relay_names[2], name3, sizeof(s_light_config.relay_names[2]) - 1);
        s_light_config.relay_names[2][sizeof(s_light_config.relay_names[2]) - 1] = '\0';
    }
    LIGHT_CONTROL_save_config_to_flash();
    // Re-initialize GPIOs based on new config (configure all possible relay pins)
    for (int i = 0; i < 3; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << relay_gpios[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level(relay_gpios[i], 0); // Explicitly set low
        s_relay_states[i] = false;
    }
    for (int i = 0; i < s_light_config.system_type; i++) {
        gpio_set_direction(relay_gpios[i], GPIO_MODE_OUTPUT); // Ensure configured pins are output
    }
    ESP_LOGI(TAG, "System config updated to %d-relay system", s_light_config.system_type);
}

void LIGHT_CONTROL_set_relay(int index, bool state)
{
    if (index >= 0 && index < s_light_config.system_type) {
        s_relay_states[index] = state;
        gpio_set_level(relay_gpios[index], state ? 1 : 0);
        ESP_LOGI(TAG, "Relay %d set to %s", index, state ? "ON" : "OFF");
    }
}

bool LIGHT_CONTROL_get_relay_state(int index)
{
    if (index >= 0 && index < s_light_config.system_type) {
        return s_relay_states[index];
    }
    return false;
}

int LIGHT_CONTROL_get_system_type(void)
{
    return s_light_config.system_type;
}

const char* LIGHT_CONTROL_get_relay_name(int index)
{
    if (index >= 0 && index < s_light_config.system_type) {
        return s_light_config.relay_names[index];
    }
    return ""; // Return empty string for out-of-bounds access
}

/* Private Function Implementation -----------------------------------------*/
static void LIGHT_CONTROL_save_config_to_flash(void)
{
    // Redirect to the master config save function in mqtt.c
    // This ensures all settings (MQTT + Relays) are persisted together in "config"
    MQTT_Update_config_save_to_flash();
}

void LIGHT_CONTROL_get_default_settings(int *system_type, char (*relay_names)[32])
{
    if (system_type) *system_type = 3;
    if (relay_names) {
        strncpy(relay_names[0], "gate1", sizeof(relay_names[0]) - 1);
        relay_names[0][sizeof(relay_names[0]) - 1] = '\0';
        strncpy(relay_names[1], "gate2", sizeof(relay_names[1]) - 1);
        relay_names[1][sizeof(relay_names[1]) - 1] = '\0';
        strncpy(relay_names[2], "patio", sizeof(relay_names[2]) - 1);
        relay_names[2][sizeof(relay_names[2]) - 1] = '\0';
    }
}
