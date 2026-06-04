/**
 * @file system.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2023-08-29
 * 
 * @copyright Copyright (c) 2023
 * 
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SYSTEM_H
#define __SYSTEM_H

#ifdef __cplusplus
extern "C"
{
#endif
/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
  /**
 * enum of all the system states
 */

  typedef enum
  {
    SYSTEM_INIT,
    SYSTEM_TIME,
    TERMINAL_CONSOLE_INIT,
    SYSTEM_TIMER,
    FLASH_INIT,
    USAGE_INIT,
    WIFI_MANAGER_INIT,
    WIFI_PROVISIONING_INIT,
    AUTO_TIME_SYNC_INIT,    
    OTA_INIT,
    MQTT_INIT,
    LTE_INIT,
    BUTTON_INIT,
    LIGHT_CONTROL_INIT_STATE,
    SYSTEM_MAIN
  } system_states_e;

  /**
 * Initialise this layer
 */
  void System_init();

  /**
 * The layer tasks function
 */
  void System_tasks();

  /**
 * The layer tick function
 */

  void System_tick_1ms();

/**
 * @brief Returns true if the system has any active internet connection (Wi-Fi or LTE).
 */
bool System_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_H */