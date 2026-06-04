# The following lines of code register the main component of the project.
# Added spi_flash for esp_flash.h and checked managed component naming.

idf_component_register(
    SRCS 
        "src/main.c"
        "src/system.c"
        "src/time_management.c"
        "src/terminal_console.c"
        "src/flash.c"
        "src/wifi_manager.c"
        "src/wifi_provisioning.c"
        "src/auto_time_sync.c"
        "src/ota.c"
        "src/mqtt.c"
        "src/lte.c"
        "src/data_usage.c"
        "src/button.c"
        "src/light_control.c"
    INCLUDE_DIRS 
        "inc"
    PRIV_REQUIRES
        esp_timer
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        esp_http_server
        esp_https_ota
        esp_https_server
        app_update
        driver
        nvs_flash
        mqtt
        spiffs
        json
        spi_flash
        espressif__esp_modem  # Explicitly using the managed component namespace
)

# Create the SPIFFS partition image from the 'data' directory
spiffs_create_partition_image(storage ../data FLASH_IN_PROJECT)