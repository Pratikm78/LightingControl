/**
 * @file light_control.h
 * @author Your Name
 * @brief Header file for the light_control module.
 * @version 0.1
 * @date 2026-06-02
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef LIGHT_CONTROL_H
#define LIGHT_CONTROL_H

/* Includes ------------------------------------------------------------------*/
// Add necessary includes here, e.g., "freertos/FreeRTOS.h"
#include <stdbool.h>
/* Public Function Prototypes ----------------------------------------------*/

/**
 * @brief Initializes the light_control module.
 * @note This function should be called once at startup.
 */
void LIGHT_CONTROL_init(void);

/**
 * @brief Main task for the light_control module.
 * @note This function is designed to be called repeatedly in the main system loop.
 */
void LIGHT_CONTROL_tasks(void);

/**
 * @brief System tick handler for the light_control module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void LIGHT_CONTROL_tick_1ms(void);


int LIGHT_CONTROL_get_system_type(void);
bool LIGHT_CONTROL_get_relay_state(int index);
void LIGHT_CONTROL_set_relay(int index, bool state);
const char* LIGHT_CONTROL_get_relay_name(int index);
void LIGHT_CONTROL_set_system_config(int type, const char* name1, const char* name2, const char* name3);
#endif /* LIGHT_CONTROL_H */
