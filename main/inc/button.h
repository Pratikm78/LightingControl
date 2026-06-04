/**
 * @file button.h
 * @author Your Name
 * @brief Header file for the button module.
 * @version 0.1
 * @date 2026-04-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BUTTON_H
#define BUTTON_H

/* Includes ------------------------------------------------------------------*/
// Add necessary includes here, e.g., "freertos/FreeRTOS.h"
#include <stdbool.h>
/* Public Function Prototypes ----------------------------------------------*/

/**
 * @brief Initializes the button module.
 * @note This function should be called once at startup.
 */
void BUTTON_init(void);

/**
 * @brief Main task for the button module.
 * @note This function is designed to be called repeatedly in the main system loop.
 */
void BUTTON_tasks(void);

/**
 * @brief System tick handler for the button module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void BUTTON_tick_1s(void);
/**
 * get button state
 */
bool BUTTON_get_state(void);
/**
 * clear button state
 */
void BUTTON_clear_state(void);


#endif /* BUTTON_H */
