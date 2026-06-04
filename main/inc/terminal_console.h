/**
 * @file terminalConsole.h
 * @author your name
 * @brief Terminal console module to send commands via uart0
 * @version 0.1
 * @date 2021-08-11
 * 
 * @copyright Copyright (c) 2021
 * 
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __TERMINAL_CONSOLE_H
#define __TERMINAL_CONSOLE_H

#ifdef __cplusplus
extern "C"
{
#endif
#define packetSize 5120
/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
  /* Exported types ------------------------------------------------------------*/


  /* Exported constants --------------------------------------------------------*/

  /* Exported macro ------------------------------------------------------------*/

  /**
 * Initialise this layer
 */
  void TERMINAL_CONSOLE_init();

  /**
 * The layer tasks function
 */
  void TERMINAL_CONSOLE_tasks();

  /**
 * The layer tick function
 */

  void TERMINAL_CONSOLE_tick_1ms();


/** @brief decode commands from external source
*
* @param command message to be decoded
* @param length message length
*/
void decodeCommandExternal(uint8_t *command, uint16_t length);

/**
 * @brief De-initializes the terminal console.
 */
void TERMINAL_CONSOLE_deinit();
  /* Private defines -----------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* __TERMINAL_CONSOLE_H */