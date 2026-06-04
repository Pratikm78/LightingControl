/**
 * @file ota.h
 * @author Your Name
 * @brief Header file for the ota module.
 * @version 0.1
 * @date 2025-11-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef OTA_H
#define OTA_H

/* Includes ------------------------------------------------------------------*/
// Add necessary includes here, e.g., "freertos/FreeRTOS.h"

/* Public Function Prototypes ----------------------------------------------*/

/**
 * @brief Initializes the ota module.
 * @note This function should be called once at startup.
 */
void OTA_init(void);

/**
 * @brief Main task for the ota module.
 * @note This function is designed to be called repeatedly in the main system loop.
 */
void OTA_tasks(void);

/**
 * @brief System tick handler for the ota module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void OTA_tick_1hr(void);

/**
 * @brief  Force an immediate check for firmware update.
 * 
 */
void OTA_force_check_for_update(void);

#endif /* OTA_H */
