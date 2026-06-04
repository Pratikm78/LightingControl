/**
 * @file terminalConsole.c
 * @author your name
 * @brief Terminal console module to send and receive commands via USB Serial/JTAG.
 * @version 0.1
 * @date 2021-08-11
 *
 * @copyright Copyright (c) 2021
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "terminal_console.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "time_management.h"
#include "esp_vfs_dev.h"
#include "esp_spiffs.h"
#include "system.h"
#include "cJSON.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "flash.h"
#include "ota.h"
#include "data_usage.h"
/* Private variables ---------------------------------------------------------*/
char incomming_byte[1];                   /**< Buffer for incoming byte (currently unused but cleared). */
char command_buffer[packetSize];          /**< Buffer to hold the command string from the terminal. */
bool decode_msg = false;                  /**< Flag to trigger message decoding (currently unused). */
char command_buffer_external[packetSize]; /**< Buffer for commands received from an external source. */
bool external_msg = false;                /**< Flag indicating a command from an external source is ready. */

/**
 * @brief Structure to hold a key-value pair from a decoded command.
 */
typedef struct
{
    char key[35];           /**< The key part of the command. */
    char value[packetSize]; /**< The value part of the command. */
} keyValue_t;

/**
 * @brief Global instance for the parsed key-value pair.
 */
keyValue_t keyValuePair;

const char *command_ls[] = {
    "reboot",
    "commands",
    "get",
    "listdir",
    "readfile",
    "writefile",
    "appendfile",
    "deletefile",
    "fileexists",
    "filesize",
    "ota"
}; /**< List of supported command strings. */   

/**
 * @brief Enumeration for mapping command strings to integer values.
 */
enum command_options
{
    reboot,
    commands,
    get,
    listdir,
    readfile,
    writefile,
    appendfile,
    deletefile,
    fileexists,
    filesize,
    ota,
    max_commands
};
bool terminal_debug = false;

/**
 * @brief Structure to hold a file command with filename and content.
 */
typedef struct
{

    char filename[35];        /**< The name of the file. */
    char content[packetSize]; /**< The content for the file. */
} fileCommand_t;

fileCommand_t fileCommandPair;

char *TAG_Terminal = "Terminal Console";
/* Private Functions ---------------------------------------------------------*/

/**
 * @brief Decodes and executes a command string.
 * @note All commands are expected to be single key-value pairs.
 *       See API A Datasheet Rev1-0 (created by Ivan Potter) for more details.
 * @param msg The command string payload to be decoded.
 */
void decode_command(char *msg);

/**
 * @brief Separates a command string into its key and value components.
 * @param command valid key/value parsed for extraction.
 * @param extracted is the pointer to the struct of type keyValue_t
 * @return void.
 */
void extractKVP(char *command, keyValue_t *extracted);

/**
 * @brief Extracts the file name and file contents from a command string.
 *
 * @param command valid file command parsed for extraction
 * @param extracted is the pointer to the struct of type fileCommand_t
 */
void extractFileCommand(char *command, fileCommand_t *extracted);

/* Code Implementation -------------------------------------------------------*/

/**
 * Initialise this layer
 */
void TERMINAL_CONSOLE_init()
{
    // Allow USB peripheral to stabilize after reset
    vTaskDelay(pdMS_TO_TICKS(100));

    // First uninstall any existing driver instance
    usb_serial_jtag_driver_uninstall();
    
    // Wait for the uninstall to complete
    vTaskDelay(pdMS_TO_TICKS(50));

    // Install the direct USB Serial/JTAG driver for raw, character-by-character I/O
    usb_serial_jtag_driver_config_t usb_serial_jtag_config = {
        .rx_buffer_size = 1024,
        .tx_buffer_size = 1024,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_serial_jtag_config));

    // Allow driver to initialize
    vTaskDelay(pdMS_TO_TICKS(50));

    // NOTE: Standard printf/ESP_LOGI will no longer work over USB.
    const char *init_msg = "Terminal Console Initialized :)\r\n";
    usb_serial_jtag_write_bytes(init_msg, strlen(init_msg), portMAX_DELAY);
}

/**
 * @brief De-initializes the terminal console.
 */
void TERMINAL_CONSOLE_deinit()
{
    ESP_LOGI(TAG_Terminal, "De-initializing terminal console...");
    usb_serial_jtag_driver_uninstall();
}

/**
 * The layer tasks function
 */
void TERMINAL_CONSOLE_tasks()
{
    // This is a simple echo example to test console input.
    uint8_t c;
    // Read one byte with a small timeout to remain non-blocking
    int len = usb_serial_jtag_read_bytes(&c, 1, 20 / portTICK_PERIOD_MS);

    if (len > 0)
    { // A character was received
        // Check for Enter key (carriage return)
        if (c == '\r' || c == '\n')
        {
            const char *newline = "\r\n";
            usb_serial_jtag_write_bytes(newline, strlen(newline), portMAX_DELAY);
            // Trim trailing whitespace and decode the command
            command_buffer[strcspn(command_buffer, "\r\n")] = 0;
            decode_command(command_buffer);
            // Clear the buffer for the next line
            memset(command_buffer, 0, sizeof(command_buffer));
        }
        // Handle backspace
        else if (c == '\b' || c == 127)
        {
            if (strlen(command_buffer) > 0)
            {
                const char *backspace_seq = "\b \b";
                usb_serial_jtag_write_bytes(backspace_seq, strlen(backspace_seq), portMAX_DELAY);
                command_buffer[strlen(command_buffer) - 1] = '\0';
            }
        }
        // Handle a printable character
        else if (c >= ' ' && c <= '~' && strlen(command_buffer) < sizeof(command_buffer) - 1)
        {
            command_buffer[strlen(command_buffer)] = c;
            usb_serial_jtag_write_bytes(&c, 1, portMAX_DELAY); // Echo character
        }
    }
    if (external_msg) // This handles commands from other sources
    {
        decode_command(command_buffer_external);
    }
}

/**
 * The layer tick function
 */

void TERMINAL_CONSOLE_tick_1ms()
{
}

/**
 * @brief Decodes and executes a command string.
 * @note All commands are expected to be single key-value pairs.
 *       See API A Datasheet Rev1-0 (created by Ivan Potter) for more details.
 * @param msg The command string payload to be decoded.
 * @return None
 */
void decode_command(char *msg)
{

    bool bfound = false;
    if (*msg == ':')
    {
        const char *err_msg = "Please Enter a Key:Value pair\r\n";
        usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
        external_msg = false;
        // printf("%lukb free\n", esp_get_free_heap_size() / 1024); // This will no longer work
        return;
    }
    // Check if the message (after trimming whitespace) is empty or doesn't have a colon
    // Only check if the message is empty or contains only whitespace.
    if (strspn(msg, " \t\r\n") == strlen(msg))
    {
        external_msg = false;
        const char *err_msg = "Invalid Command [type commands:all for help]\r\n";
        usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
        return;
    }
    else
    {
        extractKVP(msg, &keyValuePair);

        int command_item = -1;
        for (int i = 0; i < max_commands; i++)
        {
            if (strcasecmp(keyValuePair.key, command_ls[i]) == 0)
            {
                command_item = i;
                bfound = true;
            }
            else
            {
                char *temp = strstr(keyValuePair.key, command_ls[i]);
                if (temp != NULL)
                {
                    int len = strlen(command_ls[i]);
                    int len_temp = strlen(temp);
                    len_temp = len_temp - 1;
                    if (len == len_temp)
                    {
                        command_item = i;
                        bfound = true;
                    }
                }
            }
            if (bfound)
            {
                break;
            }
        }
        if (!bfound)
        {
            const char *err_msg = "Invalid Command [type commands:all for help]\r\n";
            usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
        }
        {
            // if (terminal_debug)
            // printf("Command:%d === %s\n", command_item, command_ls[command_item]);
            switch (command_item)
            {
            case reboot:
                if (strcasecmp(keyValuePair.value, "1") == 0)
                {
                    esp_restart();
                }
                else
                {
                    const char *err_msg = "Error\r\n";
                    usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                }
                break;
            case commands:
                if (strcasecmp(keyValuePair.value, "all") == 0)
                {
                    const char *list_msg = "List of available commands\r\n";
                    usb_serial_jtag_write_bytes(list_msg, strlen(list_msg), portMAX_DELAY);
                    for (int i = 0; i < max_commands; i++)
                    {
                        char temp_buf[64];
                        snprintf(temp_buf, sizeof(temp_buf), "\t%s : <value>\r\n", command_ls[i]);
                        usb_serial_jtag_write_bytes(temp_buf, strlen(temp_buf), portMAX_DELAY);
                    }
                }
                else
                {
                    const char *err_msg = "Error\r\n";
                    usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                }
                break;
            case get:
                if (strcasecmp(keyValuePair.value, "time") == 0)
                {
                    char time_str[30];
                    TIME_MANAGEMENT_get_time_string(time_str, sizeof(time_str));
                    char temp_buf[40];
                    snprintf(temp_buf, sizeof(temp_buf), "%s\r\n", time_str);
                    usb_serial_jtag_write_bytes(temp_buf, strlen(temp_buf), portMAX_DELAY);
                    char uptime_buf[40];
                    snprintf(uptime_buf, sizeof(uptime_buf), "Uptime: %ld mins\r\n", TIME_MANAGEMENT_get_runtime_in_minutes());
                    usb_serial_jtag_write_bytes(uptime_buf, strlen(uptime_buf), portMAX_DELAY);
                    char wifi_conn_buf[60];
                    snprintf(wifi_conn_buf, sizeof(wifi_conn_buf), "WiFi Connected Time: %ld mins\r\n", TIME_MANAGEMENT_get_wifi_connected_minutes());
                    usb_serial_jtag_write_bytes(wifi_conn_buf, strlen(wifi_conn_buf), portMAX_DELAY);
                    char wifi_discon_buf[70];
                    snprintf(wifi_discon_buf, sizeof(wifi_discon_buf), "WiFi Disconnected Time: %ld mins\r\n", TIME_MANAGEMENT_get_wifi_disconnected_minutes());
                    usb_serial_jtag_write_bytes(wifi_discon_buf, strlen(wifi_discon_buf), portMAX_DELAY);
                }
                else if (strcasecmp(keyValuePair.value, "usage") == 0)
                {
                  /*  char usage_buf[128];
                    snprintf(usage_buf, sizeof(usage_buf), 
                             "Data Usage:\r\n\tSession: %" PRIu32 " KB\r\n\tMonthly: %" PRIu32 " KB\r\n\tTotal:   %" PRIu32 " KB\r\n", 
                             DATA_USAGE_get_session_kb(), 
                             DATA_USAGE_get_monthly_kb(), 
                             DATA_USAGE_get_total_kb());
                    usb_serial_jtag_write_bytes(usage_buf, strlen(usage_buf), portMAX_DELAY);
                */}
                break;
            case listdir:
                if (strcasecmp(keyValuePair.value, "all") == 0)
                {
                    const char *list_msg = "Listing all files in /spiffs directory:\r\n";
                    usb_serial_jtag_write_bytes(list_msg, strlen(list_msg), portMAX_DELAY);
                    FLASH_list_dir(NULL, 0); // Print directly to terminal
                }
                else
                {
                    const char *err_msg = "Error\r\n";
                    usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                }
                break;
            case readfile:
                // Clear content buffer before reading
                memset(fileCommandPair.content, 0, sizeof(fileCommandPair.content));
                if (FLASH_read_file(keyValuePair.value, fileCommandPair.content, sizeof(fileCommandPair.content)) == ESP_OK)
                {
                    char header_buf[128];
                    // Safely print the header, truncating the filename if it's too long.
                    int max_filename_len = sizeof(header_buf) - strlen("File: \r\nContents:\r\n") - 1;
                    // Print header and content in separate, safe steps
                    snprintf(header_buf, sizeof(header_buf), "File: %.*s\r\nContents:\r\n", max_filename_len, keyValuePair.value);

                    usb_serial_jtag_write_bytes(header_buf, strlen(header_buf), portMAX_DELAY);
                    usb_serial_jtag_write_bytes(fileCommandPair.content, strlen(fileCommandPair.content), portMAX_DELAY);
                    usb_serial_jtag_write_bytes("\r\n", 2, portMAX_DELAY);
                }
                else
                {
                    const char *err_msg = "Error reading file\r\n";
                    usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                }
                break;
            case writefile:
                if ((strstr(keyValuePair.value, "|")) != NULL)
                {
                    extractFileCommand(keyValuePair.value, &fileCommandPair);
                    if (FLASH_write_to_file(fileCommandPair.filename, fileCommandPair.content) == ESP_OK)
                    {
                        const char *succ_msg = "File written successfully\r\n";
                        usb_serial_jtag_write_bytes(succ_msg, strlen(succ_msg), portMAX_DELAY);
                    }
                    else
                    {
                        const char *err_msg = "Error writing file\r\n";
                        usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                    }
                }
                else
                {
                    const char *err_msg = "Error: Use format filename|content\r\n";
                    usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                }
                break; // Added missing break to prevent fallthrough
            case appendfile:
                if ((strstr(keyValuePair.value, "|")) != NULL)
                {
                    extractFileCommand(keyValuePair.value, &fileCommandPair);
                    if (FLASH_append_to_file(fileCommandPair.filename, fileCommandPair.content) == ESP_OK)
                    {
                        const char *succ_msg = "File appended successfully\r\n";
                        usb_serial_jtag_write_bytes(succ_msg, strlen(succ_msg), portMAX_DELAY);
                    }
                    else
                    {
                        const char *err_msg = "Error appending file\r\n";
                        usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                    }
                }
                else
                {
                    const char *err_msg = "Error: Use format filename|content\r\n";
                    usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                }
                break;
                case deletefile:
                if (FLASH_delete_file(keyValuePair.value) == ESP_OK) // Use the consistent flash API
                {
                    const char *succ_msg = "File deleted successfully\r\n";
                    usb_serial_jtag_write_bytes(succ_msg, strlen(succ_msg), portMAX_DELAY);
                }
                else
                {
                    const char *err_msg = "Error deleting file or file not found\r\n";
                    usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                }
                break;
            case fileexists:
                if (FLASH_file_exists(keyValuePair.value))
                {
                    const char *succ_msg = "File exists\r\n";
                    usb_serial_jtag_write_bytes(succ_msg, strlen(succ_msg), portMAX_DELAY);
                }
                else
                {           
                    const char *err_msg = "File does not exist\r\n";
                    usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                }
                break;
            case filesize:         
                long size = FLASH_get_file_size(keyValuePair.value);
                if (size >= 0)
                {
                    char size_buf[64];
                    snprintf(size_buf, sizeof(size_buf), "File size: %ld bytes\r\n", size);
                    usb_serial_jtag_write_bytes(size_buf, strlen(size_buf), portMAX_DELAY);
                }
                else
                {
                    const char *err_msg = "Error getting file size\r\n";
                    usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                }
                break;
            case ota:
                if (strcasecmp(keyValuePair.value, "check") == 0)
                {
                    OTA_force_check_for_update();
                    const char *msg = "OTA update check initiated\r\n";
                    usb_serial_jtag_write_bytes(msg, strlen(msg), portMAX_DELAY);
                }
                else
                {
                    const char *err_msg = "Error: Use ota:check to initiate update\r\n";
                    usb_serial_jtag_write_bytes(err_msg, strlen(err_msg), portMAX_DELAY);
                }
                break;
            default:
                break;
            }
        }

        memset(keyValuePair.value, 0x00, sizeof(keyValuePair.value));
        memset(keyValuePair.key, 0x00, sizeof(keyValuePair.key));
        memset(incomming_byte, 0x00, sizeof(incomming_byte));
    }
}

/**
 * @brief Separates a command string into its key and value components.
 * @note All commands are single key-value pairs. See API A Datasheet Rev1-0 (created by Ivan Potter) for more details.
 * @param command valid key/value parsed for extraction.
 * @param extracted is the pointer to the struct of type keyValue_t
 */
void extractKVP(char *command, keyValue_t *extracted)
{
    memset(extracted->key, 0x00, sizeof(extracted->key));
    memset(extracted->value, 0x00, sizeof(extracted->value));

    // Find the delimiter
    char *delimiter = strchr(command, ':');
    if (delimiter == NULL)
    {
        // No delimiter found, treat the whole command as the key
        strncpy(extracted->key, command, sizeof(extracted->key) - 1);
        return;
    }

    // Calculate key length and copy it
    size_t key_len = delimiter - command;
    if (key_len < sizeof(extracted->key))
    {
        strncpy(extracted->key, command, key_len);
    }

    // The rest of the string is the value
    char *value_start = delimiter + 1;
    strncpy(extracted->value, value_start, sizeof(extracted->value) - 1);

    // Trim trailing newline/carriage return from value
    extracted->value[strcspn(extracted->value, "\r\n")] = '\0';

    if (terminal_debug)
        printf("Key:'%s', Value:'%s'\n", extracted->key, extracted->value);
}

/**
 * @brief Extracts the file name and file contents from a command string.
 *
 * @param command valid file command parsed for extraction
 * @param extracted is the pointer to the struct of type fileCommand_t
 */
void extractFileCommand(char *command, fileCommand_t *extracted)
{
    const char search[2] = "|";
    char *token;
    memset(extracted->filename, 0x00, sizeof(extracted->filename));
    memset(extracted->content, 0x00, sizeof(extracted->content));
    /* get the first token */
    token = strtok(command, search);
    strcpy(extracted->filename, token);
    if (terminal_debug)
        printf("File Name:%s\n", extracted->filename);
    token = strtok(NULL, search);
    strcpy(extracted->content, token);
    if (terminal_debug)
        printf("File Contents:%s\n", extracted->content);
}

/** @brief decode commands from external source
 *
 * @param command message to be decoded
 * @param length message length
 */
void decodeCommandExternal(uint8_t *command, uint16_t length)
{
    // Use usb_serial_jtag_write_bytes as printf is disconnected
    usb_serial_jtag_write_bytes((const char *)command, strlen((const char *)command), portMAX_DELAY);
    char len_buf[32];
    int len_str = snprintf(len_buf, sizeof(len_buf), "\r\nlength %d\r\n", length);
    usb_serial_jtag_write_bytes(len_buf, len_str, portMAX_DELAY);

    memset(command_buffer_external, 0x00, sizeof(command_buffer_external));
    snprintf(command_buffer_external, length, "%s", (char *)command);
    external_msg = true;
}

/* END OF FILE ---------------------------------------------------------------*/