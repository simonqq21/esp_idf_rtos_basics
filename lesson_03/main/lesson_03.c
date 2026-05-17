#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"
// Needed for atoi()
#include <stdlib.h>

/*
Challenge:
Using FreeRTOS, create two separate tasks. One listens for an integer over UART (from the Serial Monitor)
and sets a variable when it sees an integer. The other task blinks the onboard LED (or other connected LED)
at a rate specified by that integer. In effect, you want to create a multi-threaded system that allows for
the user interface to run concurrently with the control task (the blinking LED).
 */
static const char *LED_TAG = "LED TEST";
static const char *UART_TAG = "UART TEST";
#define BUF_SIZE (1024)

/**
 * KConfig configuration
 */
#define BLINK_GPIO CONFIG_BLINK_GPIO
__uint16_t blink_interval = CONFIG_BLINK_INTERVAL;
#define UART_PORT_NUM CONFIG_UART_PORT_NUM
#define UART_BAUD_RATE CONFIG_UART_BAUD_RATE
//

static uint8_t led_state = 0;
// Configure a buffer for the incoming serial data

static void configure_gpio(void)
{
    ESP_LOGI(LED_TAG, "LED GPIO %d\n", BLINK_GPIO);
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

static void blink_LED_task(void *params)
{
    configure_gpio();
    while (1)
    {
        if (blink_interval == 0)
        {
            led_state = 0;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        else
        {
            // ESP_LOGI(LED_TAG, "Turning LED %s!", led_state == true ? "ON" : "OFF");
            /* Set the GPIO level according to the state (LOW or HIGH)*/
            vTaskDelay(blink_interval / portTICK_PERIOD_MS);
            /* Toggle the LED state */
            led_state = !led_state;
        }
        gpio_set_level(BLINK_GPIO, led_state);
    }
}

static void UART_task(void *params)
{
    char *data = (char *)malloc(BUF_SIZE);
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

    while (1)
    {
        // Read data from the UART
        int len = uart_read_bytes(UART_PORT_NUM, data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        // Write data back to the UART
        uart_write_bytes(UART_PORT_NUM, (const char *)data, len);
        // if data valid
        if (len)
        {
            blink_interval = atoi(data);
            ESP_LOGI(UART_TAG, "set blink interval to %d\n", blink_interval);
        }
    }
}

void app_main(void)
{
    xTaskCreatePinnedToCore(blink_LED_task,
                            "blink LED",
                            1024,
                            NULL,
                            1,
                            NULL,
                            0);
    xTaskCreatePinnedToCore(UART_task,
                            "UART",
                            BUF_SIZE * 2,
                            NULL,
                            1,
                            NULL,
                            0);
}
