#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"
// Needed for atoi()
#include <stdlib.h>
// needed for strtok()
#include <string.h>

/*
Challenge:
Use FreeRTOS to create two tasks and two queues.
Task A should print any new messages it receives from Queue 2. Additionally, it should read any Serial
input from the user and echo back this input to the serial input. If the user enters “delay” followed by
a space and a number, it should send that number to Queue 1.

Task B should read any messages from Queue 1. If it contains a number, it should update its delay rate
to that number (milliseconds). It should also blink an LED at a rate specified by that delay.
Additionally, every time the LED blinks 100 times, it should send the string “Blinked” to Queue 2.
You can also optionally send the number of times the LED blinked (e.g. 100) as part of struct that
encapsulates the string and this number.
*/

#define TASK1_TAG "TASK1"
#define TASK2_TAG "TASK2"
#define LED_TAG "LED"
#define UART_PORT_NUM 0
#define UART_BAUD_RATE 115200
#define LED_BLINK_LIMIT 100
#define BUF_SIZE 1024
#define BLINK_GPIO 2
#define BLINK_MAX 100
#define MSG_BUF_SIZE 20

static const char *delay_cmd = "delay ";
static const int delay_queue_len = 5; // Size of delay_queue
static const int msg_queue_len = 5;   // Size of msg_queue

/**
 * KConfig configuration
 */

/*
Queues
msg_queue contains messages sent from task 2 to task 1
msg_queue contains Blink messages sent from task 2 to task 1
delay_queue contains messages sent from task 1 to task 2
delay_queue may contain delay values sent from task 1 to task 2
*/
static QueueHandle_t msg_queue;
static QueueHandle_t delay_queue;

#define BODY_LEN 20
typedef struct
{
    char body[BODY_LEN];
    int count;
} Message;

static void configure_gpio(void)
{
    ESP_LOGI(LED_TAG, "LED GPIO %d\n", BLINK_GPIO);
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
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

/*
Task 1
Task 1 should print any new messages it receives from Queue 2.
Additionally, it should read any Serial input from the user and echo
back this input to the serial input. If the user enters “delay” followed by
a space and a number, it should send that number to Queue 1.
*/
static void task_1(void *params)
{
    ESP_LOGI(TASK1_TAG, "Running");

    // char c;
    char buf[BUF_SIZE];
    int len;
    char rcv_msg[MSG_BUF_SIZE];
    int32_t delay = 0;

    while (1)
    {
        len = uart_read_bytes(UART_PORT_NUM, buf, BUF_SIZE - 1, 20 / portTICK_PERIOD_MS);
        if (len)
        {
            // return message to user
            buf[len - 1] = '\0';
            ESP_LOGI(TASK1_TAG, " len %d %s", len, buf);
            // check for "delay" command
            // ESP_LOGI("test", "%d\n", strlen(delay_cmd));
            if (memcmp(buf, delay_cmd, strlen(delay_cmd)) == 0)
            {
                // ESP_LOGI(TASK1_TAG, "delay");
                // parse the number and send to delay_queue
                delay = atoi(buf + strlen(delay_cmd));
                ESP_LOGI(TASK1_TAG, "delay %d\n", delay);
                if (xQueueSend(delay_queue, &delay, 10) != pdTRUE)
                {
                    ESP_LOGE(TASK1_TAG, "ERROR: Could not put item on delay queue.");
                }
                else
                {
                    ESP_LOGI(TASK1_TAG, "delay sent.");
                }
            }
        }
        // check for message in the msg_queue
        if (xQueueReceive(msg_queue, (void *)&rcv_msg, 0) == pdTRUE)
        {

            // ESP_LOGI(TASK1_TAG, "%s%d\n", rcv_msg.body, rcv_msg.count);
            snprintf(buf, BUF_SIZE, "Received from task 2 msg_queue: %s\n", rcv_msg);
            uart_write_bytes(UART_PORT_NUM, (const char *)buf, strlen(buf));
        }
    }
}

/*
Task 2 should read any messages from Queue 1. If it contains a number,
it should update its delay rate to that number (milliseconds). It should
also blink an LED at a rate specified by that delay.
Additionally, every time the LED blinks 100 times, it should send the
string “Blinked” to Queue 2.
You can also optionally send the number of times the LED blinked (e.g. 100)
as part of struct that
encapsulates the string and this number.
*/
static void task_2(void *params)
{
    ESP_LOGI(TASK2_TAG, "Running");

    // Message msg;
    uint16_t blink_counter_incremental = 0;
    uint32_t blink_counter_total = 0;
    int32_t led_delay = 500;
    uint8_t led_state = 0;

    char msg_buf[MSG_BUF_SIZE];

    while (1)
    {
        // if there is data in the delay queue
        if (xQueueReceive(delay_queue, (void *)&led_delay, 10 / portTICK_PERIOD_MS) == pdTRUE)
        {
            ESP_LOGI(TASK2_TAG, "delay %d\n", led_delay);
        }
        // blink LED
        if (led_delay < 0)
        {
            led_state = 0;
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
        else if (led_delay == 0)
        {
            led_state = 1;
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
        else
        {
            led_state = !led_state;
            blink_counter_incremental++;
            vTaskDelay(led_delay / portTICK_PERIOD_MS);
        }
        gpio_set_level(BLINK_GPIO, led_state);

        // // if blinked n times, send a message to the other task
        if (blink_counter_incremental >= LED_BLINK_LIMIT)
        {
            blink_counter_total += blink_counter_incremental;
            blink_counter_incremental = 0;
            // Construct message and send
            snprintf(msg_buf, MSG_BUF_SIZE, "Blinked %lu times", blink_counter_total);
            ESP_LOGI(TASK2_TAG, "%s\n", msg_buf);
            if (xQueueSend(msg_queue, (void *)msg_buf, 10) != pdTRUE)
            {
                ESP_LOGE(TASK2_TAG, "ERROR: Could not put item on msg queue.");
            }
            else
            {
                ESP_LOGI(TASK2_TAG, "msg sent.");
            }
        }
        // vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

/*
 */
void app_main(void)
{
    configure_gpio();
    configure_uart();
    // create queues
    // msg_queue = xQueueCreate(msg_queue_len, sizeof(Message));
    delay_queue = xQueueCreate(delay_queue_len, sizeof(int32_t));
    msg_queue = xQueueCreate(msg_queue_len, MSG_BUF_SIZE * sizeof(char));
    // create tasks
    xTaskCreatePinnedToCore(task_1,
                            "task 1",
                            2048,
                            NULL,
                            1,
                            NULL,
                            0);
    xTaskCreatePinnedToCore(task_2,
                            "task 2",
                            1024,
                            NULL,
                            1,
                            NULL,
                            0);
}

// /**
//  * Solution to 05 - Queue Challenge
//  *
//  * One task performs basic echo on Serial. If it sees "delay" followed by a
//  * number, it sends the number (in a queue) to the second task. If it receives
//  * a message in a second queue, it prints it to the console. The second task
//  * blinks an LED. When it gets a message from the first queue (number), it
//  * updates the blink delay to that number. Whenever the LED blinks 100 times,
//  * the second task sends a message to the first task to be printed.
//  *
//  * Date: January 18, 2021
//  * Author: Shawn Hymel
//  * License: 0BSD
//  */

// // Use only core 1 for demo purposes
// #if CONFIG_FREERTOS_UNICORE
// static const BaseType_t app_cpu = 0;
// #else
// static const BaseType_t app_cpu = 1;
// #endif

// // Settings
// static const uint8_t buf_len = 255;     // Size of buffer to look for command
// static const char command[] = "delay "; // Note the space!
// static const int delay_queue_len = 5;   // Size of delay_queue
// static const int msg_queue_len = 5;     // Size of msg_queue
// static const uint8_t blink_max = 100;   // Num times to blink before message

// // Pins (change this if your Arduino board does not have LED_BUILTIN defined)
// static const int led_pin = LED_BUILTIN;

// // Message struct: used to wrap strings (not necessary, but it's useful to see
// // how to use structs here)
// typedef struct Message
// {
//     char body[20];
//     int count;
// } Message;

// // Globals
// static QueueHandle_t delay_queue;
// static QueueHandle_t msg_queue;

// //*****************************************************************************
// // Tasks

// // Task: command line interface (CLI)
// void doCLI(void *parameters)
// {

//     Message rcv_msg;
//     char c;
//     char buf[buf_len];
//     uint8_t idx = 0;
//     uint8_t cmd_len = strlen(command);
//     int led_delay;

//     // Clear whole buffer
//     memset(buf, 0, buf_len);

//     // Loop forever
//     while (1)
//     {

//         // See if there's a message in the queue (do not block)
//         if (xQueueReceive(msg_queue, (void *)&rcv_msg, 0) == pdTRUE)
//         {
//             Serial.print(rcv_msg.body);
//             Serial.println(rcv_msg.count);
//         }

//         // Read characters from serial
//         if (Serial.available() > 0)
//         {
//             c = Serial.read();

//             // Store received character to buffer if not over buffer limit
//             if (idx < buf_len - 1)
//             {
//                 buf[idx] = c;
//                 idx++;
//             }

//             // Print newline and check input on 'enter'
//             if ((c == '\n') || (c == '\r'))
//             {

//                 // Print newline to terminal
//                 Serial.print("\r\n");

//                 // Check if the first 6 characters are "delay "
//                 if (memcmp(buf, command, cmd_len) == 0)
//                 {

//                     // Convert last part to positive integer (negative int crashes)
//                     char *tail = buf cmd_len;
//                     led_delay = atoi(tail);
//                     led_delay = abs(led_delay);

//                     // Send integer to other task via queue
//                     if (xQueueSend(delay_queue, (void *)&led_delay, 10) != pdTRUE)
//                     {
//                         Serial.println("ERROR: Could not put item on delay queue.");
//                     }
//                 }

//                 // Reset receive buffer and index counter
//                 memset(buf, 0, buf_len);
//                 idx = 0;

//                 // Otherwise, echo character back to serial terminal
//             }
//             else
//             {
//                 Serial.print(c);
//             }
//         }
//     }
// }

// // Task: flash LED based on delay provided, notify other task every 100 blinks
// void blinkLED(void *parameters)
// {

//     Message msg;
//     int led_delay = 500;
//     uint8_t counter = 0;

//     // Set up pins
//     pinMode(LED_BUILTIN, OUTPUT);

//     // Loop forever
//     while (1)
//     {

//         // See if there's a message in the queue (do not block)
//         if (xQueueReceive(delay_queue, (void *)&led_delay, 0) == pdTRUE)
//         {

//             // Best practice: use only one task to manage serial comms
//             strcpy(msg.body, "Message received ");
//             msg.count = 1;
//             xQueueSend(msg_queue, (void *)&msg, 10);
//         }

//         // Blink
//         digitalWrite(led_pin, HIGH);
//         vTaskDelay(led_delay / portTICK_PERIOD_MS);
//         digitalWrite(led_pin, LOW);
//         vTaskDelay(led_delay / portTICK_PERIOD_MS);

//         // If we've blinked 100 times, send a message to the other task
//         counter;
//         if (counter >= blink_max)
//         {

//             // Construct message and send
//             strcpy(msg.body, "Blinked: ");
//             msg.count = counter;
//             xQueueSend(msg_queue, (void *)&msg, 10);

//             // Reset counter
//             counter = 0;
//         }
//     }
// }

// //*****************************************************************************
// // Main (runs as its own task with priority 1 on core 1)

// void setup()
// {

//     // Configure Serial
//     Serial.begin(115200);

//     // Wait a moment to start (so we don't miss Serial output)
//     vTaskDelay(1000 / portTICK_PERIOD_MS);
//     Serial.println();
//     Serial.println("---FreeRTOS Queue Solution---");
//     Serial.println("Enter the command 'delay xxx' where xxx is your desired ");
//     Serial.println("LED blink delay time in milliseconds");

//     // Create queues
//     delay_queue = xQueueCreate(delay_queue_len, sizeof(int));
//     msg_queue = xQueueCreate(msg_queue_len, sizeof(Message));

//     // Start CLI task
//     xTaskCreatePinnedToCore(doCLI,
//                             "CLI",
//                             1024,
//                             NULL,
//                             1,
//                             NULL,
//                             app_cpu);

//     // Start blink task
//     xTaskCreatePinnedToCore(blinkLED,
//                             "Blink LED",
//                             1024,
//                             NULL,
//                             1,
//                             NULL,
//                             app_cpu);

//     // Delete "setup and loop" task
//     vTaskDelete(NULL);
// }

// void loop()
// {
//     // Execution should never get here
// }