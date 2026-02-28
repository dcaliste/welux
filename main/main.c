/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"

#include <esp_http_server.h>

#include "lwip/err.h"
#include "lwip/sys.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  3

/* #if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK */
/* #define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK */
/* #define EXAMPLE_H2E_IDENTIFIER "" */
/* #elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT */
/* #define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT */
/* #define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID */
/* #elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH */
/* #define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH */
/* #define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID */
/* #endif */
/* #if CONFIG_ESP_WIFI_AUTH_OPEN */
/* #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN */
/* #elif CONFIG_ESP_WIFI_AUTH_WEP */
/* #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP */
/* #elif CONFIG_ESP_WIFI_AUTH_WPA_PSK */
/* #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK */
/* #elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK */
/* #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK */
/* #elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK */
/* #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK */
/* #elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK */
/* #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK */
/* #elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK */
/* #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK */
/* #elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK */
/* #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK */
/* #endif */
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER "famous yoko album"

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;

#define MIN(a,b) ((a) < (b) ? (a) : (b))

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

struct button_t {
    gpio_num_t gpio_num;
    gptimer_handle_t timer;
    QueueHandle_t queue;
};

static struct button_t button_open, button_close;

static bool IRAM_ATTR button_timer_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    struct button_t *button = (struct button_t*)user_ctx;
    ESP_ERROR_CHECK(gptimer_stop(button->timer));
    ESP_ERROR_CHECK(gptimer_disable(button->timer));

    BaseType_t high_task_awoken = pdFALSE;
    xQueueSendFromISR(button->queue, &button, &high_task_awoken);
    return (high_task_awoken == pdTRUE);
}

static void button_release(struct button_t *button)
{
    ESP_LOGI(TAG, "Button release");
    ESP_ERROR_CHECK(gpio_set_direction(button->gpio_num, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(button->gpio_num, GPIO_FLOATING));

    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 0));
}

static void button_timer_init(struct button_t *button, gpio_num_t gpio_num, uint duration, QueueHandle_t queue)
{
    ESP_ERROR_CHECK(gpio_set_direction(gpio_num, GPIO_MODE_OUTPUT));
    button->gpio_num = gpio_num;
    /* Button emulator timer */
    gptimer_config_t buttonCfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 10000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&buttonCfg, &button->timer));
    gptimer_event_callbacks_t buttonCallback = {
        .on_alarm = button_timer_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(button->timer, &buttonCallback, button));
    gptimer_alarm_config_t alarmCfg = {
        .reload_count = 0, // counter will reload with 0 on alarm event
        .alarm_count = 10 * duration, // period = 100ms @resolution 10kHz
        .flags.auto_reload_on_alarm = false, // disable auto-reload
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(button->timer, &alarmCfg));
    button->queue = queue;

    ESP_ERROR_CHECK(gpio_set_direction(button->gpio_num, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(button->gpio_num, GPIO_FLOATING));
}

/* An HTTP POST handler */
static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static esp_err_t gpio_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret, remaining = req->content_len;

    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf,
                        MIN(remaining, sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;

        /* Log data received */
        ESP_LOGI(TAG, "%s: %.*s", req->uri, ret, buf);
    }

    ESP_LOGI(TAG, "%s: Button press", req->uri);
    struct button_t *button = (struct button_t*)req->user_ctx;
    ESP_ERROR_CHECK(gpio_set_direction(button->gpio_num, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(button->gpio_num, 0));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 1));
    ESP_ERROR_CHECK(gptimer_enable(button->timer));
    ESP_ERROR_CHECK(gptimer_start(button->timer));

    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t veluxOpen = {
    .uri       = "/api/velux/open",
    .method    = HTTP_POST,
    .handler   = gpio_post_handler,
    .user_ctx  = &button_open
};

static const httpd_uri_t veluxClose = {
    .uri       = "/api/velux/close",
    .method    = HTTP_POST,
    .handler   = gpio_post_handler,
    .user_ctx  = &button_close
};

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown URL on this server.");
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &veluxOpen);
        httpd_register_uri_handler(server, &veluxClose);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
         * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    ESP_LOGI(TAG, "Welux");
    wifi_init_sta();

    QueueHandle_t buttonQueue = xQueueCreate(1, sizeof(struct button_t*));

    button_timer_init(&button_open, GPIO_NUM_15, 500);
    button_timer_init(&button_close, GPIO_NUM_4, 500);

    /* Debug onboard led */
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 0));

    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               &disconnect_handler, &server));

    /* Start the server for the first time */
    server = start_webserver();

    while (server) {
        struct button_t *button;
        if (xQueueReceive(buttonQueue, &button, pdMS_TO_TICKS(50))) {
            button_release(button);
        }
    }
}
