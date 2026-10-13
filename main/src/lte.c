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
#include "ping/ping_sock.h"
#include "lwip/inet.h"
#include <string.h>

static const char *TAG = "LTE_GATE";

#define MODEM_TX_IO        4
#define MODEM_RX_IO        5
#define MODEM_BAUD         115200
#define LTE_UART_PORT      UART_NUM_1

#define LTE_SIGNAL_RETRIES             20
#define LTE_SIGNAL_RETRY_DELAY_MS      2000
#define LTE_ESCAPE_DELAY_MS            1100
#define LTE_HANGUP_DELAY_MS            500
#define LTE_RECONNECT_TIMEOUT_SEC      60
#define LTE_POST_CONNECT_PING_DELAY_SEC 10
#define LTE_PING_FAIL_LIMIT            5
#define LTE_PING_COUNT                 3

static esp_modem_dce_t *s_dce = NULL;
static esp_netif_t *s_netif = NULL;
static esp_ping_handle_t s_ping_handle = NULL;

static bool s_ready = false;
static bool s_initialized = false;
static bool s_event_handlers_registered = false;
static bool s_init_in_progress = false;
static bool s_ping_in_progress = false;
static bool s_force_reinit_pending = false;
static bool s_run_ping_pending = false;

static uint32_t s_reconnect_timer = 0;
static uint32_t s_post_connect_ping_delay_sec = 0;
static int s_ping_fail_count = 0;

static bool LTE_tick_3hr_timer = false;

/* Keep these global names if other modules reference them */
int rssi = -1;
bool LTE_10min_counter = false;

typedef struct
{
    bool any_reply;
    uint32_t success_count;
    uint32_t timeout_count;
} lte_ping_stats_t;

static lte_ping_stats_t s_ping_stats = {0};

/* -------------------------------------------------------------------------- */
/* Forward declarations                                                       */
/* -------------------------------------------------------------------------- */
static void LTE_cleanup_ping_session(void);
static void LTE_schedule_reinit(void);
static void LTE_handle_ping_result(bool success);
static void LTE_run_ping_test(void);

static void on_ping_success(esp_ping_handle_t hdl, void *args);
static void on_ping_timeout(esp_ping_handle_t hdl, void *args);
static void on_ping_end(esp_ping_handle_t hdl, void *args);

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data);

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static void LTE_schedule_reinit(void)
{
    s_force_reinit_pending = true;
}

static void LTE_cleanup_ping_session(void)
{
    if (s_ping_handle != NULL)
    {
        esp_ping_delete_session(s_ping_handle);
        s_ping_handle = NULL;
    }

    s_ping_in_progress = false;
    memset(&s_ping_stats, 0, sizeof(s_ping_stats));
}

static void LTE_handle_ping_result(bool success)
{
    if (success)
    {
        s_ping_fail_count = 0;
    }
    else
    {
        s_ping_fail_count++;
        ESP_LOGW(TAG, "Ping watchdog fail count: %d/%d",
                 s_ping_fail_count, LTE_PING_FAIL_LIMIT);

        if (s_ping_fail_count >= LTE_PING_FAIL_LIMIT)
        {
            ESP_LOGE(TAG, "Internet appears dead despite PPP link. Scheduling LTE reinit...");
            s_ping_fail_count = 0;
            LTE_schedule_reinit();
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Ping callbacks                                                             */
/* -------------------------------------------------------------------------- */

static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl = 0;
    uint32_t elapsed_time = 0;
    uint32_t recv_len = 0;
    ip_addr_t target_addr;

    memset(&target_addr, 0, sizeof(target_addr));

    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));

    s_ping_stats.any_reply = true;
    s_ping_stats.success_count++;

    ESP_LOGW(TAG, "!!! INTERNET ACTIVE !!! Ping 8.8.8.8: %db, time=%dms",
             (int)recv_len, (int)elapsed_time);
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    s_ping_stats.timeout_count++;
    ESP_LOGE(TAG, "!!! DATA FAILURE !!! Ping 8.8.8.8 Timeout.");
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    ESP_LOGI(TAG, "Ping session finished. success=%lu timeout=%lu",
             (unsigned long)s_ping_stats.success_count,
             (unsigned long)s_ping_stats.timeout_count);

    LTE_handle_ping_result(s_ping_stats.any_reply);
    LTE_cleanup_ping_session();
}

/* -------------------------------------------------------------------------- */
/* Ping runner                                                                */
/* -------------------------------------------------------------------------- */

static void LTE_run_ping_test(void)
{
    if (!s_ready)
    {
        ESP_LOGW(TAG, "Skipping ping test: LTE link not ready");
        return;
    }

    if (s_ping_in_progress)
    {
        ESP_LOGW(TAG, "Skipping ping test: previous ping still in progress");
        return;
    }

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    esp_ping_callbacks_t cbs = {
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end = on_ping_end,
        .cb_args = NULL
    };

    ip_addr_t target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    ipaddr_aton("8.8.8.8", &target_addr);

    ping_config.target_addr = target_addr;
    ping_config.count = LTE_PING_COUNT;

    memset(&s_ping_stats, 0, sizeof(s_ping_stats));

    esp_err_t err = esp_ping_new_session(&ping_config, &cbs, &s_ping_handle);
    if (err != ESP_OK || s_ping_handle == NULL)
    {
        ESP_LOGE(TAG, "esp_ping_new_session failed: %s", esp_err_to_name(err));
        s_ping_handle = NULL;
        LTE_handle_ping_result(false);
        return;
    }

    s_ping_in_progress = true;

    err = esp_ping_start(s_ping_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ping_start failed: %s", esp_err_to_name(err));
        LTE_cleanup_ping_session();
        LTE_handle_ping_result(false);
        return;
    }
}

/* -------------------------------------------------------------------------- */
/* IP event handler                                                           */
/* -------------------------------------------------------------------------- */

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
        s_reconnect_timer = 0;

        /* Do NOT block the event loop task here. */
        s_post_connect_ping_delay_sec = LTE_POST_CONNECT_PING_DELAY_SEC;
        s_run_ping_pending = false;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_PPP_LOST_IP)
    {
        ESP_LOGE(TAG, "PPP LOST IP");
        s_ready = false;
        s_post_connect_ping_delay_sec = 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

void LTE_init(void)
{
    if (s_init_in_progress)
    {
        ESP_LOGW(TAG, "LTE_init already in progress, skipping re-entry");
        return;
    }

    s_init_in_progress = true;

    esp_log_level_set("esp_modem", ESP_LOG_VERBOSE);
    esp_log_level_set("ppp", ESP_LOG_VERBOSE);

    ESP_LOGI(TAG, "Starting LTE Initialization Sequence...");

    s_ready = false;
    s_initialized = false;
    s_post_connect_ping_delay_sec = 0;
    s_run_ping_pending = false;

    /* Stop any active ping session cleanly before tearing down modem/network. */
    if (s_ping_in_progress && s_ping_handle != NULL)
    {
        esp_ping_stop(s_ping_handle);

        /* Give ping callbacks time to finish and delete session */
        for (int i = 0; i < 20 && s_ping_in_progress; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        /* Fallback cleanup if callback didn't finish in time */
        if (s_ping_in_progress)
        {
            ESP_LOGW(TAG, "Forcing ping session cleanup during LTE reinit");
            LTE_cleanup_ping_session();
        }
    }

    if (s_dce != NULL)
    {
        esp_modem_destroy(s_dce);
        s_dce = NULL;
    }

    uart_config_t uart_config = {
        .baud_rate = MODEM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_is_driver_installed(LTE_UART_PORT))
    {
        uart_driver_delete(LTE_UART_PORT);
    }

    ESP_ERROR_CHECK(uart_driver_install(LTE_UART_PORT, 8192, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(LTE_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(LTE_UART_PORT, MODEM_TX_IO, MODEM_RX_IO, -1, -1));

    /* Escape to command mode and hang up old session */
    vTaskDelay(pdMS_TO_TICKS(LTE_ESCAPE_DELAY_MS));
    uart_write_bytes(LTE_UART_PORT, "+++", 3);
    vTaskDelay(pdMS_TO_TICKS(LTE_ESCAPE_DELAY_MS));
    uart_write_bytes(LTE_UART_PORT, "ATH\r\n", 5);
    vTaskDelay(pdMS_TO_TICKS(LTE_HANGUP_DELAY_MS));

    uart_driver_delete(LTE_UART_PORT);

    if (s_netif == NULL)
    {
        esp_netif_config_t ppp_cfg = ESP_NETIF_DEFAULT_PPP();
        s_netif = esp_netif_new(&ppp_cfg);
        if (s_netif == NULL)
        {
            ESP_LOGE(TAG, "Failed to create PPP netif");
            s_init_in_progress = false;
            return;
        }
    }

    if (!s_event_handlers_registered)
    {
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, &ip_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, &ip_event_handler, NULL));
        s_event_handlers_registered = true;
    }

    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.port_num = LTE_UART_PORT;
    dte_config.uart_config.tx_io_num = MODEM_TX_IO;
    dte_config.uart_config.rx_io_num = MODEM_RX_IO;
    dte_config.uart_config.rx_buffer_size = 8192;

    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG("internet");

    s_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dte_config, &dce_config, s_netif);
    if (s_dce == NULL)
    {
        ESP_LOGE(TAG, "esp_modem_new_dev failed");
        s_init_in_progress = false;
        return;
    }

    int ber = -1;
    rssi = -1;

    for (int retry = 0; retry < LTE_SIGNAL_RETRIES; retry++)
    {
        esp_err_t err = esp_modem_get_signal_quality(s_dce, &rssi, &ber);
        if (err == ESP_OK && rssi != 99)
        {
            ESP_LOGI(TAG, "Network Found. Signal: %d", rssi);
            break;
        }

        ESP_LOGI(TAG, "Searching... (%d/%d)", retry, LTE_SIGNAL_RETRIES);
        vTaskDelay(pdMS_TO_TICKS(LTE_SIGNAL_RETRY_DELAY_MS));
    }

    char imei[32] = {0};
    char imsi[32] = {0};

    esp_modem_get_imsi(s_dce, imsi);
    esp_modem_get_imei(s_dce, imei);
    ESP_LOGW(TAG, "Hardware IDs -> IMSI: %s | IMEI: %s", imsi, imei);

    ESP_LOGI(TAG, "Requesting PPP Data Session...");
    esp_err_t mode_err = esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
    if (mode_err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_modem_set_mode(DATA) failed: %s", esp_err_to_name(mode_err));
        s_init_in_progress = false;
        return;
    }

    s_initialized = true;
    s_reconnect_timer = 0;
    s_init_in_progress = false;
}

bool LTE_is_ready(void)
{
    return s_ready;
}

void LTE_tasks(void)
{
    if (s_force_reinit_pending)
    {
        s_force_reinit_pending = false;
        LTE_init();
        return;
    }

    if (s_post_connect_ping_delay_sec == 0 && s_run_ping_pending && s_ready)
    {
        ESP_LOGI(TAG, "Running delayed post-connect ping test...");
        s_run_ping_pending = false;
        LTE_run_ping_test();
    }

    if (LTE_10min_counter && s_ready)
    {
        ESP_LOGI(TAG, "Starting periodic 10-minute internet check...");
        LTE_run_ping_test();
        LTE_10min_counter = false;
    }

    /* If not ready, the 1-second tick will handle timeout-based LTE reinit */
}

void LTE_tick_1s(void)
{
    if (s_initialized && !s_ready)
    {
        s_reconnect_timer++;
        if (s_reconnect_timer >= LTE_RECONNECT_TIMEOUT_SEC)
        {
            ESP_LOGE(TAG, "Connection Timeout. Retrying LTE Init...");
            s_reconnect_timer = 0;
            LTE_schedule_reinit();
        }
    }
    else
    {
        s_reconnect_timer = 0;
    }

    if (s_post_connect_ping_delay_sec > 0)
    {
        s_post_connect_ping_delay_sec--;
        if (s_post_connect_ping_delay_sec == 0)
        {
            s_run_ping_pending = true;
        }
    }
}

void LTE_tick_3hr(void)
{
    LTE_tick_3hr_timer = true;
}

int LTE_get_rssi(void)
{
    return rssi;
}

void LTE_tick_10min(void)
{
    LTE_10min_counter = true;
}
