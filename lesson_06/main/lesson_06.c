#include <stdio.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

/*
Starting with the code given below, modify it to protect the task
parameter (delay_arg) with a mutex. With the mutex in place, the
task should be able to read the parameter (parameters) into the
local variable (num) before the calling function’s stack memory
goes out of scope (the value given by delay_arg).
 */

/**
 * FreeRTOS Mutex Challenge
 *
 * Pass a parameter to a task using a mutex.
 *
 * Date: January 20, 2021
 * Author: Shawn Hymel
 * License: 0BSD
 */

#define LED_TAG "LED"
#define INPUT_TAG "UART_INPUT"
#define UART_PORT_NUM 0
#define UART_BAUD_RATE 115200
#define LED_BLINK_LIMIT 100
#define BUF_SIZE 1024
#define LED_GPIO 2
#define BLINK_MAX 100
#define MSG_BUF_SIZE 20

char buf[BUF_SIZE];

// mutex
static SemaphoreHandle_t mutex;

//*****************************************************************************
// Tasks

static void configure_gpio(void)
{
    ESP_LOGI(LED_TAG, "LED GPIO %d\n", LED_GPIO);
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

// Blink LED based on rate passed by parameter
void blinkLED(void *parameters)

{
    // portMAX_DELAY means infinite timeout
    xSemaphoreTake(mutex, portMAX_DELAY);
    uint8_t led_state = 0;
    ESP_LOGI(LED_TAG, "starting blinkLED task.");
    // Copy the parameter into a local variable
    int num = *(int *)parameters;

    // Print the parameter
    ESP_LOGI(INPUT_TAG, "Received: %d", num);
    xSemaphoreGive(mutex);
    ESP_LOGI(INPUT_TAG, "Mutex given.");

    // Configure the LED pin
    configure_gpio();

    // Blink forever and ever
    while (1)
    {
        led_state = !led_state;
        vTaskDelay(num / portTICK_PERIOD_MS);
        gpio_set_level(LED_GPIO, led_state);
    }
}

static void configure_uart(void)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        // .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    /**
     * If you want to use USB, set TXD to 1 and RXD to 3.
     */
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, 1, 3, 0, 0));
}

void app_main(void)
{
    long int delay_arg;
    uint16_t len;
    configure_uart();

    // Wait a moment to start (so we don't miss Serial output)
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(INPUT_TAG, " ");
    ESP_LOGI(INPUT_TAG, "---FreeRTOS Mutex Challenge---");
    ESP_LOGI(INPUT_TAG, "Enter a number for delay (milliseconds)");

    mutex = xSemaphoreCreateMutex();

    // read a non-negative integer from UART

    do
    {
        len = uart_read_bytes(UART_PORT_NUM, buf, BUF_SIZE - 1, 1000 / portTICK_PERIOD_MS);
    } while (len == 0);

    if (len)
    {
        delay_arg = atoi(buf);

        ESP_LOGI(INPUT_TAG, "delay_arg inside app_main = %d", delay_arg);
    }

    // ESP_LOGI("MAIN", "starting blinkLED task.");

    // Start task 1
    xTaskCreatePinnedToCore(blinkLED,
                            "Blink LED",
                            1024,
                            (void *)&delay_arg,
                            1,
                            NULL,
                            0);
    // Show that we accomplished our task of passing the stack-based argument
    vTaskDelay(10 / portTICK_PERIOD_MS);
    xSemaphoreTake(mutex, portMAX_DELAY);
    ESP_LOGI(INPUT_TAG, "Done!");

    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    /*
    A mutex cannot be taken and given from two different tasks. It will result in runtime errors.
    */
}
