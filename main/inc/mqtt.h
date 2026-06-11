/**
 * @file mqtt.h
 * @author Your Name
 * @brief Header file for the mqtt module.
 * @version 0.1
 * @date 2026-03-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef MQTT_H
#define MQTT_H

/* Includes ------------------------------------------------------------------*/
// Add necessary includes here, e.g., "freertos/FreeRTOS.h"

/* Public Function Prototypes ----------------------------------------------*/

/**
 * @brief Initializes the mqtt module.
 * @note This function should be called once at startup.
 */
void MQTT_init(void);

/**
 * @brief Main task for the mqtt module.
 * @note This function is designed to be called repeatedly in the main system loop.
 */
void MQTT_tasks(void);

/**
 * @brief System tick handler for the mqtt module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void MQTT_tick_1sec(void);

/**
 * @brief System tick handler for the mqtt module.
 * @note This function is called from a timer ISR, so it must be fast and non-blocking.
 */
void MQTT_tick_1min(void);

void MQTT_Update_config_save_to_flash();



#endif /* MQTT_H */
