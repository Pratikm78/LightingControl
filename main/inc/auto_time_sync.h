/**
 * @file auto_time_sync.h
 * @author Your Name
 * @brief Header file for the auto_time_sync module.
 * @version 0.1
 * @date 2025-10-16
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef AUTO_TIME_SYNC_H
#define AUTO_TIME_SYNC_H

/* Includes ------------------------------------------------------------------*/
// Add necessary includes here, e.g., "freertos/FreeRTOS.h"

/* Public Function Prototypes ----------------------------------------------*/

/**
 * @brief Initializes the auto_time_sync module.
 * @note This function should be called once at startup.
 */
void AUTO_TIME_SYNC_init(void);

/**
 * @brief Main task for the auto_time_sync module.
 * @note This function is designed to be called repeatedly in the main system loop.
 */
void AUTO_TIME_SYNC_tasks(void);

/**
 * @brief System tick handler for the auto_time_sync module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void AUTO_TIME_SYNC_tick_1min(void);

#endif /* AUTO_TIME_SYNC_H */
