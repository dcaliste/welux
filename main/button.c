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

static void button_timer_callback(void *arg)
{
    button_release((struct button_t*)arg);
}

void button_press(struct button_t *button)
{
    ESP_LOGI(TAG, "Button press");
    ESP_ERROR_CHECK(gpio_set_direction(button->gpio_num, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(button->gpio_num, 0));

    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 1));

    ESP_ERROR_CHECK(esp_timer_start_once(button->timer, button->duration));
}

void button_release(struct button_t *button)
{
    ESP_LOGI(TAG, "Button release");
    ESP_ERROR_CHECK(gpio_set_direction(button->gpio_num, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(button->gpio_num, GPIO_FLOATING));

    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 0));
}

void button_init(struct button_t *button, gpio_num_t gpio_num, unsigned int duration)
{
    button->gpio_num = gpio_num;
    ESP_ERROR_CHECK(gpio_set_direction(button->gpio_num, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(button->gpio_num, GPIO_FLOATING));

    const esp_timer_create_args_t timer_args = {
        .callback = &button_timer_callback,
        .arg = (void*)button,
        .name = "release"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &button->timer));
    button->duration = duration * 1000;
}
