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

#include "button.h"
#include "logging.h"

static bool IRAM_ATTR button_timer_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    struct button_t *button = (struct button_t*)user_ctx;
    ESP_ERROR_CHECK(gptimer_stop(button->timer));
    ESP_ERROR_CHECK(gptimer_disable(button->timer));

    BaseType_t high_task_awoken = pdFALSE;
    xQueueSendFromISR(button->queue, &button, &high_task_awoken);
    return (high_task_awoken == pdTRUE);
}

void IRAM_ATTR button_press(struct button_t *button)
{
    ESP_LOGI(TAG, "Button press");
    ESP_ERROR_CHECK(gpio_set_direction(button->gpio_num, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(button->gpio_num, 0));

    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 1));

    ESP_ERROR_CHECK(gptimer_enable(button->timer));
    ESP_ERROR_CHECK(gptimer_set_raw_count(button->timer, 0)); 
    ESP_ERROR_CHECK(gptimer_start(button->timer));
}

void IRAM_ATTR button_release(struct button_t *button)
{
    ESP_LOGI(TAG, "Button release");
    ESP_ERROR_CHECK(gpio_set_direction(button->gpio_num, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(button->gpio_num, GPIO_FLOATING));

    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 0));
}

void button_init(struct button_t *button, gpio_num_t gpio_num, unsigned int duration, QueueHandle_t queue)
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
