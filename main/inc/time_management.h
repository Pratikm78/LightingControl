/**
 * @file time_management.h
 * @author your name
 * @brief 
 * @version 0.1
 * @date 2021-08-30
 * 
 * @copyright Copyright (c) 2021
 * 
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __TIME_MANAGEMENT_H
#define __TIME_MANAGEMENT_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
  /* Exported types ------------------------------------------------------------*/

  /* Exported constants --------------------------------------------------------*/

  /* Exported macro ------------------------------------------------------------*/

  /**
 * Initialise this layer
 */
  void TIME_MANAGEMENT_init();

  /**
 * The layer tasks function
 */
  void TIME_MANAGEMENT_tasks();

  /**
 * The layer tick function
 */

  void TIME_MANAGEMENT_tick_1min();

/**
 * @brief get the current time as a string
 * @param buffer Pointer to the character buffer to store the time string.
 * @param buffer_size Size of the provided buffer.
 */
void TIME_MANAGEMENT_get_time_string(char *buffer, size_t buffer_size);

/**
 * @brief epoch time
 * 
 * @return uint32_t returns the current epoch time
 */
long long TIME_MANAGEMENT_get_epoch_time();

/**
 * @brief sets the current epoch time
 * 
 * @param epoch 
 */
void TIME_MANAGEMENT_set_epoch_time(long long epoch);

/**
 * @brief operational age of device since power up
 * 
 * @return uint32_t returns runtime in minutes
 */
uint32_t TIME_MANAGEMENT_get_runtime_in_minutes();

/**
 * @brief operational wifi connected time in minutes
 * 
 * @return uint32_t 
 */
uint32_t TIME_MANAGEMENT_get_wifi_connected_minutes();

/**
 * @brief operational wifi disconnected time in minutes
 * 
 * @return uint32_t 
 */
uint32_t TIME_MANAGEMENT_get_wifi_disconnected_minutes();

#ifdef __cplusplus
}
#endif

#endif /* __TIME_MANAGEMENT_H */