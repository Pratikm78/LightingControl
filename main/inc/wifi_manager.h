/**
 * @file wifi_manager.h
 * @author Your Name
 * @brief Header file for the wifi_manager module.
 * @version 0.1
 * @date 2025-10-02
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include "esp_err.h"

/* Public Function Prototypes ----------------------------------------------*/

/**
 * @brief Initializes the wifi_manager module.
 * @note This function should be called once at startup.
 */
void WIFI_MANAGER_init(void);


/**
 * @brief Main task for the wifi_manager module.
 * @note This function is designed to be called repeatedly in the main system loop. It handles the logic for starting station or SoftAP mode.
 */
void WIFI_MANAGER_tasks(void);

/**
 * @brief System tick handler for the wifi_manager module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void WIFI_MANAGER_tick_1min(void);

/**
 * @brief indicates if the wifi credentials are configured or not.
 */
bool WIFI_MANAGER_is_configured(void);

/**
 * @brief indicates wifi connection status
 */
bool WIFI_MANAGER_is_connected(void);

/**
 * @brief returns the rssi value
 */
int WIFI_MANAGER_get_rssi(void);

/**
 * @brief sets the wifi credentials by writing to flash.
 * @param ssid The Wi-Fi network SSID.
 * @param password The Wi-Fi network password.
 */
void WIFI_MANAGER_set_credentials(const char *ssid, const char *password);


/**
 * @brief Configures Wi-Fi with new credentials and attempts to connect.
 *
 * @param ssid The SSID to connect to.
 * @param password The password for the network.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t WIFI_MANAGER_configure_and_connect(const char *ssid, const char *password);

#endif /* WIFI_MANAGER_H */
