/**
 * @file lte.h
 * @author Your Name
 * @brief Header file for the lte module.
 * @version 0.1
 * @date 2026-04-11
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef LTE_H
#define LTE_H

/* Includes ------------------------------------------------------------------*/
// Add necessary includes here, e.g., "freertos/FreeRTOS.h"
#include <stdbool.h>
/* Public Function Prototypes ----------------------------------------------*/

/**
 * @brief Initializes the lte module.
 * @note This function should be called once at startup.
 */
void LTE_init(void);

/**
 * @brief Main task for the lte module.
 * @note This function is designed to be called repeatedly in the main system loop.
 */
void LTE_tasks(void);

/**
 * @brief System tick handler for the lte module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void LTE_tick_1s(void);

/**
 * @brief Returns the connection status of the LTE modem.
 * 
 * @return true if registered and ready for data.
 */
bool LTE_is_ready(void);

/**
 * get rssi value
 */
int LTE_get_rssi(void);

void LTE_tick_3hr(void);

void LTE_tick_10min(void);

#endif /* LTE_H */
