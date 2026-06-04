/**
 * @file button.c
 * @author Your Name
 * @brief Source file for the button module.
 * @version 0.1
 * @date 2026-04-27
 *
 * @copyright Copyright (c) 2026
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "button.h"
#include "esp_log.h"
#include "driver/gpio.h"

/* Private Defines -----------------------------------------------------------*/
static const char *TAG = "Button";
static int s_last_level = -1;
static bool s_button_pressed = false;

/* Private Variables ---------------------------------------------------------*/
// Define module-specific variables here

/* Private Function Prototypes -----------------------------------------------*/
// Define private functions here

/* Public Function Implementation ------------------------------------------*/

#define BUTTON_GPIO GPIO_NUM_10


void BUTTON_init(void)
{
    // 1. Configure Button (Input)
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&btn_conf);



    // Set initial state
    s_last_level = gpio_get_level(BUTTON_GPIO);


    ESP_LOGI(TAG, "Initialized. Button: GPIO 10");
}

void BUTTON_tasks(void)
{
    int current_level = gpio_get_level(BUTTON_GPIO);

    if (current_level != s_last_level)
    {
        // Simple debounce delay
        //  vTaskDelay(pdMS_TO_TICKS(20));
        current_level = gpio_get_level(BUTTON_GPIO);

        if (current_level != s_last_level)
        {
            s_last_level = current_level;

            if (current_level == 0) // Pressed
            {
                ESP_LOGI(TAG, "Button Pressed");
                s_button_pressed = true;
               
            }
            else // Released
            {
                ESP_LOGI(TAG, "Button Released");
            }
        }
    }
}

void BUTTON_tick_1s(void)
{
    // No longer needed for basic detection, but kept for API compatibility
}

/* Private Function Implementation -----------------------------------------*/
// Implement private functions here
/**
 * get button state
 */
bool BUTTON_get_state(void)
{
    return s_button_pressed;
}
/**
 * clear button state
 */
void BUTTON_clear_state(void)
{
    s_button_pressed = false;
}