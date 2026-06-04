/**
 * @file flash.h
 * @author Your Name
 * @brief Header file for the flash module.
 * @version 0.1
 * @date 2025-10-01
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef FLASH_H
#define FLASH_H

/* Includes ------------------------------------------------------------------*/
#include "esp_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "esp_err.h"
/* Public Function Prototypes ----------------------------------------------*/

/**
 * @brief Initializes the flash module.
 * @note This function should be called once at startup.
 */
void FLASH_init(void);

/**
 * @brief Main task for the flash module.
 * @note This function is designed to be called repeatedly in the main system loop.
 */
void FLASH_tasks(void);

/**
 * @brief System tick handler for the flash module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void FLASH_tick_1ms(void);

/**
 * @brief Writes a buffer to a file in the SPIFFS partition, adding a .txt extension.
 *
 * @param filename The name of the file (without path or extension).
 * @param buffer The null-terminated string to write to the file.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t FLASH_write_to_file(const char *filename, const char *buffer);

/**
 * @brief Reads the content of a file with a .txt extension from the SPIFFS partition.
 *
 * @param filename The name of the file to read (without path or extension).
 * @param buffer Buffer to store the file content.
 * @param buffer_size The size of the buffer.
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if file doesn't exist, or other error code on failure.
 */
esp_err_t FLASH_read_file(const char *filename, char *buffer, size_t buffer_size);

/**
 * @brief Lists all files in the SPIFFS directory.
 *
 * @param file_list Buffer to store the list of files. If NULL, prints to the terminal.
 * @param list_size The size of the file_list buffer.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t FLASH_list_dir(char *file_list, size_t list_size);

/**
 * @brief Deletes a file with a .txt extension from the SPIFFS partition.
 *
 * @param filename The name of the file to delete (without path or extension).
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t FLASH_delete_file(const char *filename);

/**
 * @brief Checks if a file with a .txt extension exists in the SPIFFS partition.
 *
 * @param filename The name of the file to check (without path or extension).
 * @return true if the file exists, false otherwise.
 */
bool FLASH_file_exists(const char *filename);

/**
 * @brief Determines the size of a file with a .txt extension in bytes.
 *
 * @param filename The name of the file (without path or extension).
 * @return long The size of the file in bytes, or -1 on error.
 */
long FLASH_get_file_size(const char *filename);

/**
 * @brief Appends a buffer to a file in the SPIFFS partition, adding a .txt extension.
 *
 * @param filename The name of the file (without path or extension).
 * @param buffer The null-terminated string to append to the file.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t FLASH_append_to_file(const char *filename, const char *buffer);

#endif /* FLASH_H */
