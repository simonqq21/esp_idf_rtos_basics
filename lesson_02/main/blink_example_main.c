/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "example";

/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO_1 CONFIG_BLINK_GPIO_1
#define BLINK_GPIO_2 CONFIG_BLINK_GPIO_2
#define BLINK_PERIOD_1 CONFIG_BLINK_PERIOD_1
#define BLINK_PERIOD_2 CONFIG_BLINK_PERIOD_2

static uint8_t s_led_states[2] = {1, 1};

static void blink_LED1(void *param)
{
    while (1)
    {
        ESP_LOGI(TAG, "Turning LED1 %s!", s_led_states[0] == true ? "ON" : "OFF");
        /* Set the GPIO level according to the state (LOW or HIGH)*/
        gpio_set_level(BLINK_GPIO_1, s_led_states[0]);
        vTaskDelay(BLINK_PERIOD_1 / portTICK_PERIOD_MS);
        /* Toggle the LED state */
        s_led_states[0] = !s_led_states[0];
    }
}

static void blink_LED2(void *param)
{
    while (1)
    {
        ESP_LOGI(TAG, "Turning LED2 %s!", s_led_states[1] == true ? "ON" : "OFF");
        /* Set the GPIO level according to the state (LOW or HIGH)*/
        gpio_set_level(BLINK_GPIO_2, s_led_states[1]);
        vTaskDelay(BLINK_PERIOD_2 / portTICK_PERIOD_MS);
        /* Toggle the LED state */
        s_led_states[1] = !s_led_states[1];
    }
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO_1);
    gpio_reset_pin(BLINK_GPIO_2);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO_1, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLINK_GPIO_2, GPIO_MODE_OUTPUT);
}

void app_main(void)
{

    /* Configure the peripheral according to the LED type */
    configure_led();

    /**
     * @param function function to call
     * @param task_name task name
     * @param stack_size stack size
     * @param params params to pass to function
     * @param priority task priority
     * @param handle task handle
     * @param core CPU core to run the thread
     */
    xTaskCreatePinnedToCore(blink_LED1,
                            "blink LED 1",
                            1024,
                            NULL,
                            1,
                            NULL,
                            0);
    xTaskCreatePinnedToCore(blink_LED2,
                            "blink LED 2",
                            1024,
                            NULL,
                            1,
                            NULL,
                            0);
    while (1)
    {
    }
}
