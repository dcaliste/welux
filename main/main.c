/*
 * Copyright (C) Damien Caliste <dcaliste@free.fr>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#include <string.h>
#include <unistd.h>

#include "wifi.h"
#include "button.h"
#include "webserver.h"
#include "logging.h"
#include "bme280.h"

#include <nvs_flash.h>
#include <mdns.h>
#include <esp_pm.h>
#include <driver/i2c_master.h>

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (httpd_stop(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
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

    esp_pm_config_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 80,
        .light_sleep_enable = true
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    ESP_LOGI(TAG, "Welux");
    wifi_init_sta();

    ESP_ERROR_CHECK(mdns_init());
    /* ESP_ERROR_CHECK(mdns_hostname_set("lou")); */
    /* ESP_ERROR_CHECK(mdns_instance_name_set("Velux controller")); */
    ESP_ERROR_CHECK(mdns_hostname_set("home"));
    ESP_ERROR_CHECK(mdns_instance_name_set("Home controller"));
    /* ESP_ERROR_CHECK(mdns_hostname_set("test")); */
    /* ESP_ERROR_CHECK(mdns_instance_name_set("Test home controller")); */

    /* Debug onboard led */
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 0));

    static httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               &disconnect_handler, &server));

    server = start_webserver();

    /* struct remote_t velux; */
    /* remote_init(&velux, "velux", LOW, 500, */
    /*             GPIO_NUM_5,   // D5 */
    /*             GPIO_NUM_17,  // TX2 */
    /*             GPIO_NUM_16); // RX2 */
    /* add_remote(server, &velux); */
    /* add_sensor(server, &dev_handle); */

    struct remote_t shutter1;
    remote_init(&shutter1, "Bay window shutter", HIGH, 500,
                GPIO_NUM_18,  // D18
                GPIO_NUM_19,  // D19
                GPIO_NUM_21); // D21
    add_remote(server, &shutter1);
    struct remote_t shutter2;
    remote_init(&shutter2, "Sliding window shutter", HIGH, 500,
                GPIO_NUM_25,  // D25
                GPIO_NUM_26,  // D26
                GPIO_NUM_27); // D27
    add_remote(server, &shutter2);
    i2c_master_bus_handle_t bus_handle;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_23, // D23
        .scl_io_num = GPIO_NUM_22, // D22
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
    ESP_LOGI(TAG, "I2C initialized successfully");

    Bme280Device dev_handle;
    bme280_add_to_bus(bus_handle, &dev_handle, 0x76);
    add_sensor(server, &dev_handle);

    while (server) {
        usleep(10000000);
    }
}
