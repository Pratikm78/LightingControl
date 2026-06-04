#include "lte.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_modem_api.h"
#include "esp_modem_config.h"
#include "esp_modem_c_api_types.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ping/ping_sock.h" // ICMP Ping Support

static const char *TAG = "LTE_GATE";

#define MODEM_TX_IO 4
#define MODEM_RX_IO 5
#define MODEM_BAUD 115200
#define LTE_UART_PORT UART_NUM_1

static esp_modem_dce_t *s_dce = NULL;
static esp_netif_t *s_netif = NULL;
static bool s_ready = false;
static bool s_initialized = false;
static uint32_t s_reconnect_timer = 0;
static bool LTE_tick_3hr_timer = false;
static int s_ping_fail_count = 0; // Internet watchdog counter
int rssi = -1;
bool LTE_10min_counter = false;
/**
 * @brief Logic to handle watchdog based on ping results
 */
void LTE_handle_ping_result(bool success)
{
    if (success)
    {
        s_ping_fail_count = 0;
    }
    else
    {
        s_ping_fail_count++;
        if (s_ping_fail_count >= 5)
        { // If 5 ping tests fail in a row
            ESP_LOGE(TAG, "Internet dead despite having IP. Forcing Modem Reset...");
            s_ping_fail_count = 0;
            LTE_init(); // Force fresh PPP handshake and carrier session
        }
    }
}

/**
 * @brief Ping Callbacks
 */
static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));

    ESP_LOGW(TAG, "!!! INTERNET ACTIVE !!! Ping 8.8.8.8: %db, time=%dms", (int)recv_len, (int)elapsed_time);
    LTE_handle_ping_result(true);
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    ESP_LOGE(TAG, "!!! DATA FAILURE !!! Ping 8.8.8.8 Timeout. SIM likely has 0 balance.");
    LTE_handle_ping_result(false);
}

void LTE_run_ping_test(void)
{
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ip_addr_t target_addr;
    ipaddr_aton("8.8.8.8", &target_addr);
    ping_config.target_addr = target_addr;
    ping_config.count = 3;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .cb_args = NULL};

    esp_ping_handle_t ping;
    esp_ping_new_session(&ping_config, &cbs, &ping);
    esp_ping_start(ping);
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_PPP_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGW(TAG, "====================================");
        ESP_LOGW(TAG, "PPP GOT IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGW(TAG, "GW: " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGW(TAG, "====================================");
        s_ready = true;

        // Wait 10 seconds for South African network stability, then test internet
        vTaskDelay(pdMS_TO_TICKS(10000));
        LTE_run_ping_test();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_PPP_LOST_IP)
    {
        ESP_LOGE(TAG, "PPP LOST IP");
        s_ready = false;
    }
}

void LTE_init(void)
{
    esp_log_level_set("esp_modem", ESP_LOG_VERBOSE);
    esp_log_level_set("ppp", ESP_LOG_VERBOSE);

    ESP_LOGI(TAG, "Starting LTE Initialization Sequence...");

    if (s_dce != NULL)
    {
        esp_modem_destroy(s_dce);
        s_dce = NULL;
    }
    s_ready = false;

    uart_config_t uart_config = {
        .baud_rate = MODEM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_is_driver_installed(LTE_UART_PORT))
        uart_driver_delete(LTE_UART_PORT);
    uart_driver_install(LTE_UART_PORT, 8192, 0, 0, NULL, 0);
    uart_param_config(LTE_UART_PORT, &uart_config);
    uart_set_pin(LTE_UART_PORT, MODEM_TX_IO, MODEM_RX_IO, -1, -1);

    vTaskDelay(pdMS_TO_TICKS(1100));
    uart_write_bytes(LTE_UART_PORT, "+++", 3);
    vTaskDelay(pdMS_TO_TICKS(1100));
    uart_write_bytes(LTE_UART_PORT, "ATH\r\n", 5);
    vTaskDelay(pdMS_TO_TICKS(500));
    uart_driver_delete(LTE_UART_PORT);

    if (s_netif == NULL)
    {
        s_netif = esp_netif_new(&(esp_netif_config_t)ESP_NETIF_DEFAULT_PPP());
        esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, &ip_event_handler, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, &ip_event_handler, NULL);
    }

    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.port_num = LTE_UART_PORT;
    dte_config.uart_config.tx_io_num = MODEM_TX_IO;
    dte_config.uart_config.rx_io_num = MODEM_RX_IO;
    dte_config.uart_config.rx_buffer_size = 8192;

    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG("internet");

    s_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dte_config, &dce_config, s_netif);

    int retry = 0, ber = -1;
    while (retry < 20)
    {
        if (esp_modem_get_signal_quality(s_dce, &rssi, &ber) == ESP_OK && rssi != 99)
        {
            ESP_LOGI(TAG, "Network Found. Signal: %d", rssi);
            break;
        }
        ESP_LOGI(TAG, "Searching... (%d/20)", retry);
        retry++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    char imei[32], imsi[32];
    esp_modem_get_imsi(s_dce, imsi);
    esp_modem_get_imei(s_dce, imei);
    ESP_LOGW(TAG, "Hardware IDs -> IMSI: %s | IMEI: %s", imsi, imei);

    // char ussd_res[256] = {0};
    // ESP_LOGI(TAG, "Requesting Phone Number via USSD (*123*888#)...");
    // esp_err_t ussd_err = esp_modem_at(s_dce, "AT+CUSD=1,\"*123*888#\",15", ussd_res, 10000);

    // if (ussd_err == ESP_OK)
    // {
    //     ESP_LOGW(TAG, "USSD Response: %s", ussd_res);
    // }
    // else
    // {
    //     ESP_LOGE(TAG, "USSD Command Failed: %s", esp_err_to_name(ussd_err));
    // }

    ESP_LOGI(TAG, "Requesting PPP Data Session...");
    esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
    s_initialized = true;
}

bool LTE_is_ready(void) { return s_ready; }

void LTE_tasks(void)
{
    if (LTE_10min_counter && s_ready)
    {
        ESP_LOGI(TAG, "Starting periodic 10-minute internet check...");
        LTE_run_ping_test();
        LTE_10min_counter = false;
    }
    // if (LTE_tick_3hr_timer && s_ready)
    // {
    //     LTE_tick_3hr_timer = false;
    //     ESP_LOGI(TAG, "Running Scheduled RSSI Check & Ping Watchdog...");


    //     if (esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND) == ESP_OK)
    //     {
    //         int r = -1, b = -1;
    //         vTaskDelay(pdMS_TO_TICKS(500));
    //         esp_modem_get_signal_quality(s_dce, &r, &b);
    //         rssi = r;
    //         ESP_LOGI(TAG, "RSSI Updated: %d. Returning to Data Mode.", rssi);
    //         esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
    //     }
    // }
}

void LTE_tick_1s(void)
{
    if (s_initialized && !s_ready)
    {
        s_reconnect_timer++;
        if (s_reconnect_timer >= 60)
        {
            ESP_LOGE(TAG, "Connection Timeout. Retrying LTE Init...");
            s_reconnect_timer = 0;
            LTE_init();
        }
    }
    else
    {
        s_reconnect_timer = 0;
    }
}

void LTE_tick_3hr(void) { LTE_tick_3hr_timer = true; }
int LTE_get_rssi(void) { return rssi; }

void LTE_tick_10min(void)
{
    LTE_10min_counter = true;
}