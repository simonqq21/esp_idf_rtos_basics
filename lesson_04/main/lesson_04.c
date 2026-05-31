#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

/*
Using FreeRTOS, create two separate tasks. One listens for input over UART (from the Serial
Monitor). Upon receiving a newline character (‘\n’), the task allocates a new section of heap
memory (using pvPortMalloc()) and stores the string up to the newline character in that section
of heap. It then notifies the second task that a message is ready.

The second task waits for notification from the first task. When it receives that notification,
it prints the message in heap memory to the Serial Monitor. Finally, it deletes the allocated
heap memory (using vPortFree()).
*/

static const char *TASK1_TAG = "TASK1";
static const char *TASK2_TAG = "TASK2";

/*
KConfig configuration
*/
#define UART_PORT_NUM 0
#define UART_BAUD_RATE 115200
// CONFIG_BUF_LEN
#define UART_BUF_SIZE 1024

// pointer to message
static char *msg_ptr = NULL;
// flag that message has been received
// volatile
static uint8_t msg_flag = 0;

/*
The first task listens for input over UART (from the Serial Monitor).
Upon receiving a newline character (‘\n’), the task allocates a new section of heap
memory (using pvPortMalloc()) and stores the string up to the newline character in that section
of heap. It then notifies the second task that a message is ready.
 */
static void task_1(void *params)
{
    char buf[UART_BUF_SIZE];
    char c;
    size_t num_incoming_bytes;
    uint16_t i;
    // clear entire buffer
    memset(buf, 0, UART_BUF_SIZE);

    while (1)
    {
        // Read data from the UART
        uart_get_buffered_data_len(UART_PORT_NUM, &num_incoming_bytes);
        i = 0;
        // while incoming bytes
        while (num_incoming_bytes > 0)
        {
            // ESP_LOGI(TASK1_TAG, "num_incoming_bytes = %d\n", num_incoming_bytes);
            // read one byte at a time from UART RX buffer
            uart_read_bytes(UART_PORT_NUM, &c, 1, 20 / portTICK_PERIOD_MS);
            // ESP_LOGI(TASK1_TAG, "c = %c\n", c);
            // check if buffer hasn't overflowed
            if (i < UART_BUF_SIZE)
            {
                // add char to buffer
                buf[i] = c;
                /*
                if last character read is a newline, copy it to the global msg pointer.
                terminate it with NULL.
                */
                if (c == '\n')
                {
                    buf[i] = '\0';

                    ESP_LOGI(TASK1_TAG, "buf = %s\n", buf);
                    // check if msg flag is still in use
                    if (msg_flag == 0)
                    {
                        msg_ptr = (char *)pvPortMalloc(UART_BUF_SIZE * sizeof(char));
                        configASSERT(msg_ptr);
                        // copy message
                        i++;
                        memcpy(msg_ptr, buf, i);
                        // Notify other task that message is ready
                        msg_flag = 1;
                    }
                }
                // increment index and decrement num_incoming_bytes
                i++;
            }
            num_incoming_bytes--;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

/*
The second task waits for notification from the first task. When it receives that notification,
it prints the message in heap memory to the Serial Monitor. Finally, it deletes the allocated
heap memory (using vPortFree()).
 */
static void task_2(void *params)
{
    while (1)
    {
        if (msg_flag == 1)
        {
            ESP_LOGI(TASK2_TAG, "received msg = %s\n", msg_ptr);
            vPortFree(msg_ptr);
            msg_flag = 0;
            msg_ptr = NULL;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
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

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    /**
     * If you want to use USB, set TXD to 1 and RXD to 3.
     */
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, 1, 3, 0, 0));
}

void app_main(void)
{
    configure_uart();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    xTaskCreatePinnedToCore(task_1,
                            "task 1",
                            2048,
                            NULL,
                            1,
                            NULL,
                            0);
    xTaskCreatePinnedToCore(task_2,
                            "task 2",
                            2048,
                            NULL,
                            1,
                            NULL,
                            0);
}
