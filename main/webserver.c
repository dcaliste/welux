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

#include "webserver.h"
#include "logging.h"
#include "ota.h"

#include <esp_netif_sntp.h>
#include <esp_sntp.h>
#include <esp_timer.h>

#include <string.h>
#include <time.h>

#define MAX_LOG_ENTRIES 25
struct log_entry_t
{
    char date[64];
    char label[128];
};
static struct log_entry_t _logs[MAX_LOG_ENTRIES];
static unsigned int _i_logs = 0;

#define MIN(a,b) ((a) < (b) ? (a) : (b))

static void push_log(const char *message)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    strftime(_logs[_i_logs].date, sizeof(_logs[_i_logs].date), "%c", &timeinfo);
    strncpy(_logs[_i_logs].label, message, sizeof(_logs[_i_logs].label));
    _i_logs = (_i_logs + 1) % MAX_LOG_ENTRIES;
}

static void init_log()
{
    memset(_logs, 0, sizeof(struct log_entry_t) * MAX_LOG_ENTRIES);
}

static const char* insert_logs(httpd_req_t *req, const char *page, int *len)
{
    const char tag[] = "<!-- %%LOGS%% -->";
    const char *logtag = memmem(page, *len, tag, sizeof(tag) - 1);

    if (logtag) {
        int part_len = logtag - page;
        httpd_resp_send_chunk(req, (const char *)page, part_len);
        httpd_resp_send_chunk(req, "<ul>", 4);
        for (unsigned int i = 0; i < MAX_LOG_ENTRIES; i++) {
            struct log_entry_t *entry = _logs + ((_i_logs - 1 - i + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES);
            if (entry->date[0]) {
                httpd_resp_send_chunk(req, "<li>", 4);
                httpd_resp_send_chunk(req, entry->date, strlen(entry->date));
                httpd_resp_send_chunk(req, " : <code>", 9);
                httpd_resp_send_chunk(req, entry->label, strlen(entry->label));
                httpd_resp_send_chunk(req, "</code></li>\n", 13);
            }
        }
        if (!_logs[(_i_logs - 1 + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES].date[0]) {
            httpd_resp_send_chunk(req, "<li>no entry</li>", 17);
        }
        httpd_resp_send_chunk(req, "</ul>", 5);
        *len -= part_len + sizeof(tag) - 1;
        return logtag + sizeof(tag) - 1;
    } else {
        return page;
    }
}

static const char* insert_version(httpd_req_t *req, const char *page, int *len)
{
    const char tag[] = "<!-- %%VERSION%% -->";
    const char *vtag = memmem(page, *len, tag, sizeof(tag) - 1);
    char version[16];

    if (vtag) {
        int part_len = vtag - page;
        httpd_resp_send_chunk(req, (const char *)page, part_len);
        ESP_ERROR_CHECK(firmware_version(version, sizeof(version)));
        httpd_resp_sendstr_chunk(req, version);
        *len -= part_len + sizeof(tag) - 1;
        return vtag + sizeof(tag) - 1;
    } else {
        return page;
    }
}

static esp_err_t control_get_handler(httpd_req_t *req)
{
    extern const unsigned char ctrl_start[] asm("_binary_control_index_html_start");
    extern const unsigned char ctrl_end[]   asm("_binary_control_index_html_end");
    int len = (ctrl_end - ctrl_start);

    ESP_LOGI(TAG, "%s", req->uri);
    httpd_resp_set_type(req, "text/html");
    const char *end = insert_version(req, (const char*)ctrl_start, &len);
    httpd_resp_send_chunk(req, end, len);
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char fav_start[] asm("_binary_favicon_png_start");
    extern const unsigned char fav_end[]   asm("_binary_favicon_png_end");
    const size_t fav_size = (fav_end - fav_start);
    ESP_LOGI(TAG, "%s: size %ld", req->uri, fav_size);
    httpd_resp_set_type(req, "image/png");
    httpd_resp_send(req, (const char *)fav_start, fav_size);
    return ESP_OK;
}

static void restart_callback(void *arg)
{
    esp_timer_handle_t *timer = (esp_timer_handle_t*)arg;
    esp_timer_delete(*timer);

    ESP_LOGI(TAG, "Received restart request.");
    esp_restart();
}

static esp_err_t firmware_post_handler(httpd_req_t *req)
{
    char buf[1024], ct[128];
    int ret, remaining = req->content_len;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;
    int binary_file_length = 0;

    ESP_LOGI(TAG, "%s: content-length: %d", req->uri, req->content_len);
    if (httpd_req_get_hdr_value_str(req, "Content-Type", ct, sizeof(ct)) == ESP_OK) {
        char *boundary = memmem(ct, sizeof(ct), "boundary=", 9);
        if (boundary) {
            ESP_LOGI(TAG, "boundary: '%s'", boundary + 9);
            remaining -= strlen(boundary + 9) + 8;
        }
    }
    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf,
                                  MIN(remaining, sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }
        remaining -= ret;

        if (!update_handle) {
            char *cr = memmem(buf, ret, "\r\n\r\n", 4);
            int len = cr - buf;
            ret = ret - len - 4;
            memmove(buf, cr + 4, ret);
            update_partition = esp_ota_get_next_update_partition(NULL);
            update_handle = ota_init(update_partition, buf, ret);
            if (!update_handle) {
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_FAIL;
            }
        }
        esp_err_t err = esp_ota_write(update_handle, (const void *)buf, ret);
        if (err != ESP_OK) {
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
        binary_file_length += ret;
        ESP_LOGD(TAG, "Written image length %d", binary_file_length);
    }
    ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);

    if (ota_finalise(update_handle, update_partition) != ESP_OK) {
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/remotes/");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, "File uploaded successfully");

    ESP_LOGI(TAG, "Prepare to restart the system!");
    esp_timer_handle_t timer;
    const esp_timer_create_args_t timer_args = {
        .callback = &restart_callback,
        .arg = (void*)&timer,
        .name = "restart"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_once(timer, 2*1000*1000));

    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/remotes/");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, "Root default redirection");

    return ESP_OK;
}

#define MAX_REMOTES 3
struct remote_t* _remotes[MAX_REMOTES];
unsigned int _nRemotes = 0;

static void insert_remote(httpd_req_t *req, const char *label)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "<h2>%s</h2>\n", label);
    httpd_resp_sendstr_chunk(req, buf);
    snprintf(buf, sizeof(buf), "<form action=\"/api/%s/open\" method=\"POST\">\n", label);
    httpd_resp_sendstr_chunk(req, buf);
    httpd_resp_sendstr_chunk(req, "  <input type=\"submit\" value=\"open\" />\n");
    httpd_resp_sendstr_chunk(req, "</form>\n");
    snprintf(buf, sizeof(buf), "<form action=\"/api/%s/stop\" method=\"POST\">\n", label);
    httpd_resp_sendstr_chunk(req, buf);
    httpd_resp_sendstr_chunk(req, "  <input type=\"submit\" value=\"stop\" />\n");
    httpd_resp_sendstr_chunk(req, "</form>\n");
    snprintf(buf, sizeof(buf), "<form action=\"/api/%s/close\" method=\"POST\">\n", label);
    httpd_resp_sendstr_chunk(req, buf);
    httpd_resp_sendstr_chunk(req, "  <input type=\"submit\" value=\"close\" />\n");
    httpd_resp_sendstr_chunk(req, "</form>\n");
}

static const char* insert_remotes(httpd_req_t *req, const char *page, int *len)
{
    const char tag[] = "<!-- %%REMOTES%% -->";
    const char *postag = memmem(page, *len, tag, sizeof(tag) - 1);

    if (postag) {
        int part_len = postag - page;
        httpd_resp_send_chunk(req, (const char *)page, part_len);
        if (_nRemotes > 1) {
            insert_remote(req, "All");
        } else if (!_nRemotes) {
            httpd_resp_sendstr_chunk(req, "no remotes");
        }
        for (unsigned int i = 0; i < _nRemotes; i++) {
            insert_remote(req, _remotes[i]->label);
        }
        *len -= part_len + sizeof(tag) - 1;
        return postag + sizeof(tag) - 1;
    } else {
        return page;
    }
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    extern const unsigned char index_start[] asm("_binary_remotes_index_html_start");
    extern const unsigned char index_end[]   asm("_binary_remotes_index_html_end");
    int len = (index_end - index_start);

    ESP_LOGI(TAG, "%s", req->uri);
    httpd_resp_set_type(req, "text/html");

    const char *next = insert_remotes(req, (const char*)index_start, &len);
    const char *logs = insert_logs(req, next, &len);
    const char *end = insert_version(req, logs, &len);
    httpd_resp_send_chunk(req, end, len);
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static const httpd_uri_t espControl = {
    .uri       = "/control/",
    .method    = HTTP_GET,
    .handler   = control_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t firmwareUpdate = {
    .uri       = "/control/firmware_update.html",
    .method    = HTTP_POST,
    .handler   = firmware_post_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t favicon = {
    .uri       = "/favicon.png",
    .method    = HTTP_GET,
    .handler   = favicon_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t favico = {
    .uri       = "/favicon.ico",
    .method    = HTTP_GET,
    .handler   = favicon_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t remoteControl = {
    .uri       = "/remotes/",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown URL on this server.");
    return ESP_OK;
}

static void time_set_handler(struct timeval *tv);

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 20480;
    config.lru_purge_enable = true;
    config.max_uri_handlers = (MAX_REMOTES + 1) * 3 + 10;

    init_log();

    /* Need local time for logging. */
    setenv("TZ", "CEST-02:00:00CEDT-01:00:00,M10.5.0,M3.5.0", 1);
    tzset();
    esp_sntp_config_t ntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&ntp_config);
    sntp_set_time_sync_notification_cb(time_set_handler);

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &espControl);
        httpd_register_uri_handler(server, &firmwareUpdate);
        httpd_register_uri_handler(server, &favicon);
        httpd_register_uri_handler(server, &favico);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
        httpd_register_uri_handler(server, &remoteControl);
        httpd_register_uri_handler(server, &root);

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void all_press(void *arg)
{
    const char *action = arg;
    static esp_timer_handle_t all_timer = NULL;
    if (!all_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback = &all_press,
            .arg = arg,
            .name = "next"
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &all_timer));
    }
    static unsigned int iRemote = 0;
    if (iRemote < _nRemotes) {
        if (!strcmp(action, "open")) {
            button_press(&_remotes[iRemote]->open);
        } else if (!strcmp(action, "close")) {
            button_press(&_remotes[iRemote]->close);
        } else if (!strcmp(action, "stop")) {
            button_press(&_remotes[iRemote]->stop);
        }
        iRemote += 1;
    }
    if (iRemote < _nRemotes) {
        ESP_ERROR_CHECK(esp_timer_start_once(all_timer, 1.1 * _remotes[iRemote - 1]->open.duration));
    } else {
        ESP_ERROR_CHECK(esp_timer_delete(all_timer));
        all_timer = NULL;
        iRemote = 0;
        free(arg);
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

    if (req->user_ctx) {
        button_press((struct button_t*)req->user_ctx);
    } else {
        all_press(strdup(req->uri + 9));
    }
    push_log(req->uri);

    // End response
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/remotes/");
    httpd_resp_sendstr(req, "Action performed");
    return ESP_OK;
}

static httpd_uri_t remoteOpen[MAX_REMOTES];
static httpd_uri_t remoteClose[MAX_REMOTES];
static httpd_uri_t remoteStop[MAX_REMOTES];

static const httpd_uri_t allOpen = {
    .uri       = "/api/All/open",
    .method    = HTTP_POST,
    .handler   = gpio_post_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t allClose = {
    .uri       = "/api/All/close",
    .method    = HTTP_POST,
    .handler   = gpio_post_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t allStop = {
    .uri       = "/api/All/stop",
    .method    = HTTP_POST,
    .handler   = gpio_post_handler,
    .user_ctx  = NULL
};

static void encode(char *out, int len, const char *in)
{
    int j = 0;
    for (int i = 0; in[i] && j < len - 1; i++) {
        if (in[i] == ' ') {
            out[j++] = '%';
            out[j++] = '2';
            out[j++] = '0';
        } else
            out[j++] = in[i];
    }
    out[j] = '\0';
}

void add_remote(httpd_handle_t *server, struct remote_t *remote)
{
    char buf[64];
    char encoded[32];

    encode(encoded, sizeof(encoded), remote->label);
    snprintf(buf, sizeof(buf), "/api/%s/open", encoded);
    remoteOpen[_nRemotes].uri = buf;
    remoteOpen[_nRemotes].method = HTTP_POST;
    remoteOpen[_nRemotes].handler = gpio_post_handler;
    remoteOpen[_nRemotes].user_ctx = &remote->open;
    httpd_register_uri_handler(server, remoteOpen + _nRemotes);
    snprintf(buf, sizeof(buf), "/api/%s/close", encoded);
    remoteClose[_nRemotes].uri = buf;
    remoteClose[_nRemotes].method = HTTP_POST;
    remoteClose[_nRemotes].handler = gpio_post_handler;
    remoteClose[_nRemotes].user_ctx = &remote->close;
    httpd_register_uri_handler(server, remoteClose + _nRemotes);
    snprintf(buf, sizeof(buf), "/api/%s/stop", encoded);
    remoteStop[_nRemotes].uri = buf;
    remoteStop[_nRemotes].method = HTTP_POST;
    remoteStop[_nRemotes].handler = gpio_post_handler;
    remoteStop[_nRemotes].user_ctx = &remote->stop;
    httpd_register_uri_handler(server, remoteStop + _nRemotes);

    if (_nRemotes < MAX_REMOTES)
        _remotes[_nRemotes++] = remote;

    if (_nRemotes == 1) {
        httpd_register_uri_handler(server, &allOpen);
        httpd_register_uri_handler(server, &allClose);
        httpd_register_uri_handler(server, &allStop);
    }
}

static void time_set_handler(struct timeval *tv)
{
    static bool init = false;
    struct tm timeinfo;

    if (!init) {
        push_log("initialisation");
        init = true;

        if (_nRemotes > 0) {
            localtime_r(&tv->tv_sec, &timeinfo);
            all_press((timeinfo.tm_hour > 6 && timeinfo.tm_hour < 21) ? strdup("open") : strdup("close"));
        }
    }
}
