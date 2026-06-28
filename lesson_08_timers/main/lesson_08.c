#include <stdio.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"
/*
Your challenge is to create an auto-dim feature using the onboard LED.
We’ll pretend that the onboard LED (LED_BUILTIN) is the backlight to
an LCD.

Create a task that echoes characters back to the serial terminal (as
we’ve done in previous challenges). When the first character is entered,
the onboard LED should turn on. It should stay on so long as characters
are being entered.

LED auto-dim

Use a timer to determine how long it’s been since the last character was
entered (hint: you can use xTimerStart() to restart a timer’s count,
even if it’s already running). When there has been 5 seconds of inactivity,
your timer’s callback function should turn off the LED.

*/

#define LED_GPIO 2
#define LED_TAG "LED"

static const TickType_t dim_delay = 5000 / portTICK_PERIOD_MS;
static TimerHandle_t one_shot_timer = NULL;

// configure UART
#define UART_PORT_NUM 0
#define UART_BAUD_RATE 115200
#define UART_BUF_SIZE 1024
static void uart_configure(void)
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

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    /**
     * If you want to use USB, set TXD to 1 and RXD to 3.
     */
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, 1, 3, 0, 0));
}

// configure PWM
// configure backlight GPIO
#define LEDC_OUTPUT_PIN (2)         // The GPIO pin you want to use
#define LEDC_CHANNEL LEDC_CHANNEL_0 // LEDC channel
#define LEDC_TIMER LEDC_TIMER_0     // LEDC timer
#define LEDC_MODE LEDC_LOW_SPEED_MODE
static void configure_gpio(void)
{
    ESP_LOGI(LED_TAG, "LED GPIO %d\n", LED_GPIO);
    // 1. Configure the PWM Timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_13_BIT, // 13-bit resolution (0 to 8191)
        .freq_hz = 5000,                      // 5 kHz frequency
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&ledc_timer);

    // 2. Configure the PWM Channel
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LEDC_OUTPUT_PIN,
        .duty = 0, // Start with 0% duty cycle
        .hpoint = 0};
    ledc_channel_config(&ledc_channel);
}

/*
backlight tasks
 */
#define BACKLIGHT_MAX_DUTY (8191)
#define BACKLIGHT_MIN_DUTY (128)
// active backlight task
static void bright_backlight(void)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, BACKLIGHT_MAX_DUTY);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

// dim backlight task
static void dim_backlight(void)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, BACKLIGHT_MIN_DUTY);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void dim_backlight_callback(TimerHandle_t xTimer)
{
    dim_backlight();
}

// UART CLI task
char rx_buf[UART_BUF_SIZE];
char tx_buf[UART_BUF_SIZE];
static void UART_CLI_task(void *param)
{
    uint16_t len;

    while (1)
    {
        len = uart_read_bytes(UART_PORT_NUM, rx_buf, UART_BUF_SIZE - 1, 20 / portTICK_PERIOD_MS);
        if (len)
        {
            // Process the command
            // clear UART tx buffer
            memset(tx_buf, 0, UART_BUF_SIZE);
            // print to UART tx buffer
            snprintf(tx_buf, 1024, "CLI_IN: %.1014s", rx_buf); // 1023 - 7 - 1 - 1 = 1014 characters left
            // transmit UART tx buffer
            uart_write_bytes(UART_PORT_NUM, tx_buf, strlen(tx_buf));
            // clear UART rx buffer
            memset(rx_buf, 0, UART_BUF_SIZE);
            // bright backlight
            bright_backlight();
            // start backlight dimming timer
            xTimerStart(one_shot_timer, portMAX_DELAY);
        }
    }
}

void app_main(void)
{
    uart_configure();
    configure_gpio();

    bright_backlight();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    dim_backlight();

    // Create a one-shot timer
    one_shot_timer = xTimerCreate(
        "One-shot timer",        // Name of timer
        dim_delay,               // Period of timer (in ticks)
        pdFALSE,                 // Auto-reload
        (void *)0,               // Timer ID
        dim_backlight_callback); // Callback function

    xTaskCreatePinnedToCore(UART_CLI_task,
                            "UART CLI",
                            1024,
                            NULL,
                            1,
                            NULL,
                            0);

    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
