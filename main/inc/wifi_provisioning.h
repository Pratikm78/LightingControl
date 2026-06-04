/**
 * @file wifi_provisioning.h
 * @author Your Name
 * @brief Header file for the wifi_provisioning module.
 * @version 0.1
 * @date 2025-10-09
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

/* Includes ------------------------------------------------------------------*/
// Add necessary includes here, e.g., "freertos/FreeRTOS.h"

/* Public Function Prototypes ----------------------------------------------*/

/**
 * @brief Initializes the wifi_provisioning module.
 * @note This function should be called once at startup.
 */
void WIFI_PROVISIONING_init(void);

/**
 * @brief Main task for the wifi_provisioning module.
 * @note This function is designed to be called repeatedly in the main system loop.
 */
void WIFI_PROVISIONING_tasks(void);

/**
 * @brief System tick handler for the wifi_provisioning module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void WIFI_PROVISIONING_tick_1ms(void);

#endif /* WIFI_PROVISIONING_H */
