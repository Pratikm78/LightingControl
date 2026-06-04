/**
 * @file flash.c
 * @author Reminder
 * @brief This file provides an interface for file system operations on the SPIFFS partition.
 * @version 0.1
 * @date 2025-10-01
 *
 * @copyright Copyright (c) 2025
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "flash.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <string.h>
#include "system.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include "sys/dir.h"
#include "esp_system.h"
#include "esp_flash.h"
/* Private Defines -----------------------------------------------------------*/
#define MAX_FILENAME_LENGTH 256 // Maximum length for a filename in SPIFFS
#define MAX_FILEPATH_LENGTH (sizeof("/spiffs/") + MAX_FILENAME_LENGTH)

static const char *TAG = "Flash";

/* Private Variables ---------------------------------------------------------*/
// Define module-specific variables here

/* Private Function Prototypes -----------------------------------------------*/
static esp_err_t FLASH_write_to_file_custom(const char *filename, const char *buffer);
static esp_err_t FLASH_read_file_custom(const char *filename, char *buffer, size_t buffer_size);
static esp_err_t FLASH_append_to_file_custom(const char *filename, const char *buffer);
static void build_filepath(char *filepath_buf, size_t buf_size, const char *filename, const char *extension);
static esp_err_t read_file_content(FILE *f, char *buffer, size_t buffer_size);


/* Public Function Implementation ------------------------------------------*/

void FLASH_init(void)
{
    // initialisation of spiffs general storage
    esp_vfs_spiffs_conf_t StorageConfig = {.base_path = "/spiffs",
                                           .partition_label = "storage",
                                           .max_files = 20,
                                           .format_if_mount_failed = true};
    esp_err_t ret = esp_vfs_spiffs_register(&StorageConfig);
    switch (ret)
    {
    case ESP_OK:
        ESP_LOGI(TAG, "SPIFFS storage Initalized\n");
        break;
    case ESP_FAIL:

        ESP_LOGE(TAG, "Failed to mount or format filesystem\n");
        break;
    case ESP_ERR_NOT_FOUND:

        ESP_LOGE(TAG, "Failed to find SPIFFS partition\n");
        break;

    default:
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)\n", esp_err_to_name(ret));
        break;
    }
    ESP_LOGI(TAG, "Initialized");
}

void FLASH_tasks(void)
{
    // This task will be called in a loop.
    // Add your cooperative, non-blocking logic here.
}

void FLASH_tick_1ms(void)
{
}

esp_err_t FLASH_write_to_file(const char *filename, const char *buffer)
{
    char filepath[MAX_FILEPATH_LENGTH];
    build_filepath(filepath, sizeof(filepath), filename, ".txt");
    return FLASH_write_to_file_custom(filepath, buffer);
}

esp_err_t FLASH_read_file(const char *filename, char *buffer, size_t buffer_size)
{
    char filepath[MAX_FILEPATH_LENGTH];
    build_filepath(filepath, sizeof(filepath), filename, ".txt");
    return FLASH_read_file_custom(filepath, buffer, buffer_size);
}

esp_err_t FLASH_list_dir(char *file_list, size_t list_size)
{
    DIR *dir;
    dir = opendir("/spiffs");
    if (!dir)
    {
        ESP_LOGE(TAG, "Failed to open directory /spiffs");
        return ESP_FAIL;
    }

    if (file_list)
    {
        file_list[0] = '\0'; // Ensure buffer is empty
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        char fullPath[MAX_FILEPATH_LENGTH];
        snprintf(fullPath, sizeof(fullPath), "/spiffs/%s", entry->d_name);

        char temp_buffer[MAX_FILEPATH_LENGTH + 32]; // For path + size string
        struct stat entryStat;
        if (stat(fullPath, &entryStat) == -1)
        {
            ESP_LOGE(TAG, "Error getting stats for %s", fullPath);
            continue;
        }
        else
        {
            snprintf(temp_buffer, sizeof(temp_buffer), "%s [%ld bytes]\n", fullPath, entryStat.st_size);
            if (file_list != NULL)
            {
                if (strlen(file_list) + strlen(temp_buffer) < list_size)
                {
                    strcat(file_list, temp_buffer);
                }
                else
                {
                    ESP_LOGW(TAG, "File list buffer is full, cannot add more entries.");
                    closedir(dir);
                    return ESP_ERR_NO_MEM;
                }
            }
            else
            {
                printf("%s", temp_buffer);
            }
        }
    }
    closedir(dir);
    return ESP_OK;
}

esp_err_t FLASH_delete_file(const char *filename)
{
    char filepath[MAX_FILEPATH_LENGTH];
    build_filepath(filepath, sizeof(filepath), filename, ".txt");
    if (unlink(filepath) == 0)
    {
        ESP_LOGI(TAG, "Deleted file: %s", filepath);
        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to delete file: %s", filepath);
        return ESP_FAIL;
    }
}

bool FLASH_file_exists(const char *filename)
{
    char filepath[MAX_FILEPATH_LENGTH];
    build_filepath(filepath, sizeof(filepath), filename, ".txt");
    struct stat st;
    if (stat(filepath, &st) == 0)
    {
        return true;
    }
    return false;
}

long FLASH_get_file_size(const char *filename)
{
    char filepath[MAX_FILEPATH_LENGTH];
    build_filepath(filepath, sizeof(filepath), filename, ".txt");
    
    struct stat st;
    if (stat(filepath, &st) != 0)
    {
        return -1;
    }
    
    return st.st_size;
}

esp_err_t FLASH_append_to_file(const char *filename, const char *buffer)
{
    char filepath[MAX_FILEPATH_LENGTH];
    build_filepath(filepath, sizeof(filepath), filename, ".txt");
    return FLASH_append_to_file_custom(filepath, buffer);
}

static esp_err_t FLASH_write_to_file_custom(const char *filename, const char *buffer)
{
    FILE *f = fopen(filename, "w+");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    if (fprintf(f, "%s", buffer) < 0)
    {
        ESP_LOGE(TAG, "Failed to write to file");
        fclose(f);
        return ESP_FAIL;
    }
    fclose(f);
    ESP_LOGI(TAG, "File written: %s", filename);
    return ESP_OK;
}

static esp_err_t FLASH_read_file_custom(const char *filename, char *buffer, size_t buffer_size)
{
    FILE *f = fopen(filename, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading %s", filename);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = read_file_content(f, buffer, buffer_size);

    fclose(f);
    return ret;
}

static esp_err_t FLASH_append_to_file_custom(const char *filename, const char *buffer)
{
    FILE *f = fopen(filename, "a");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for appending data");
        return ESP_FAIL;
    }
    if (fprintf(f, "%s", buffer) < 0)
    {
        ESP_LOGE(TAG, "Failed to append to file");
        fclose(f);
        return ESP_FAIL;
    }
    fclose(f);
    ESP_LOGI(TAG, "Appended to file: %s", filename);
    return ESP_OK;
}

/* Private Function Implementation -----------------------------------------*/

/**
 * @brief Builds a full SPIFFS file path.
 * @param filepath_buf Buffer to store the resulting path.
 * @param buf_size Size of the buffer.
 * @param filename The base filename.
 * @param extension The file extension to append (e.g., ".txt"). If NULL, no extension is added.
 */
static void build_filepath(char *filepath_buf, size_t buf_size, const char *filename, const char *extension)
{
    if (extension)
    {
        snprintf(filepath_buf, buf_size, "/spiffs/%s%s", filename, extension);
    }
    else
    {
        snprintf(filepath_buf, buf_size, "/spiffs/%s", filename);
    }
}

/**
 * @brief Reads content from an open file into a buffer.
 * @param f Pointer to the open file.
 * @param buffer Buffer to store the content.
 * @param buffer_size Size of the buffer.
 * @return esp_err_t ESP_OK on success, ESP_ERR_NO_MEM if buffer is too small.
 */
static esp_err_t read_file_content(FILE *f, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Clear the buffer
    memset(buffer, 0, buffer_size);

    size_t total_read = 0;
    size_t bytes_read;

    while ((bytes_read = fread(buffer + total_read, 1, buffer_size - total_read - 1, f)) > 0)
    {
        total_read += bytes_read;
    }

    if (ferror(f))
    {
        ESP_LOGE(TAG, "Failed to read file");
        return ESP_FAIL;
    }

    if (!feof(f))
    {
        ESP_LOGW(TAG, "File contents truncated, buffer too small.");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Read %d bytes from file.", total_read);
    return ESP_OK;
}
