#include <stdio.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

/**
 * FreeRTOS Counting Semaphore Challenge
 *
 * Challenge: use a mutex and counting semaphores to protect the shared buffer
 * so that each number (0 through 4) is printed exactly 3 times to the Serial
 * monitor (in any order). Do not use queues to do this!
 *
 * Hint: you will need 2 counting semaphores in addition to the mutex, one for
 * remembering number of filled slots in the buffer and another for
 * remembering the number of empty slots in the buffer.
 *
 * Date: January 24, 2021
 * Author: Shawn Hymel
 * License: 0BSD
 */

// You'll likely need this on vanilla FreeRTOS
// #include semphr.h

#define UART_PORT_NUM 0
#define UART_BAUD_RATE 115200
#define UART_BUF_SIZE 1024
#define MAIN_TAG "MAIN"
#define CONSUMER_TAG "CONSUMER"

// Use only core 1 for demo purposes
#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

// Settings
enum
{
    BUF_SIZE = 5
}; // Size of buffer array
static const int num_prod_tasks = 5; // Number of producer tasks
static const int num_cons_tasks = 2; // Number of consumer tasks
static const int num_writes = 3;     // Num times each producer writes to buf

// Globals
static int buf[BUF_SIZE]; // Shared buffer
static int head = 0;      // Writing index to buffer
static int tail = 0;      // Reading index to buffer
// this binary semaphore allows for parameter to be read safely within each producer
// task while producer tasks are being created.
static SemaphoreHandle_t bin_sem;
// this mutex protects the critical section, the part where the circular buffer
// indices are updated and the message is printed to the UART.
static SemaphoreHandle_t mutex;
// this semaphore counts the number of blank slots in the circular buffer.
static SemaphoreHandle_t sem_empty;
// this semaphore counts the number of filled slots in the circular buffer.
static SemaphoreHandle_t sem_filled;

//*****************************************************************************
// Tasks

// Producer: write a given number of times to shared buffer
void producer(void *parameters)
{

    // Copy the parameters into a local variable
    int num = *(int *)parameters;

    // Release the binary semaphore after reading the parameter
    xSemaphoreGive(bin_sem);

    // Fill shared buffer with task number
    for (int i = 0; i < num_writes; i++)
    {
        // decrement the empty slots semaphore
        xSemaphoreTake(sem_empty, portMAX_DELAY);

        // lock critical section with a mutex
        xSemaphoreTake(mutex, portMAX_DELAY);
        // Critical section (accessing shared buffer)
        buf[head] = num;
        head = (head + 1) % BUF_SIZE;
        // unlock critical section mutex
        xSemaphoreGive(mutex);

        // increment the filled slots semaphore
        xSemaphoreGive(sem_filled);
    }

    // Delete self task
    vTaskDelete(NULL);
}

// Consumer: continuously read from shared buffer
void consumer(void *parameters)
{

    int val;

    // Read from buffer
    while (1)
    {
        // decrement the filled slots semaphore
        xSemaphoreTake(sem_filled, portMAX_DELAY);

        // Critical section (accessing shared buffer and Serial)
        // lock critical section with a mutex
        xSemaphoreTake(mutex, portMAX_DELAY);
        val = buf[tail];
        tail = (tail + 1) % BUF_SIZE;
        ESP_LOGI(CONSUMER_TAG, "val = %d", val);
        // unlock critical section mutex
        xSemaphoreGive(mutex);

        // increment the empty slots semaphore
        xSemaphoreGive(sem_empty);
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
    char task_name[24];
    configure_uart();

    // Wait a moment to start (so we don't miss Serial output)
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(MAIN_TAG, "---FreeRTOS Semaphore Alternate Solution---");
    // Create mutexes and semaphores before starting tasks
    bin_sem = xSemaphoreCreateBinary();
    mutex = xSemaphoreCreateMutex();
    // max count, initial count
    sem_empty = xSemaphoreCreateCounting(BUF_SIZE, BUF_SIZE);
    sem_filled = xSemaphoreCreateCounting(BUF_SIZE, 0);

    // Start producer tasks (wait for each to read argument)
    for (int i = 0; i < num_prod_tasks; i++)
    {
        sprintf(task_name, "Producer %i", i);
        xTaskCreatePinnedToCore(producer,
                                task_name,
                                1024,
                                (void *)&i,
                                1,
                                NULL,
                                app_cpu);
        xSemaphoreTake(bin_sem, portMAX_DELAY);
    }

    // Start consumer tasks
    for (int i = 0; i < num_cons_tasks; i++)
    {
        sprintf(task_name, "Consumer %i", i);
        xTaskCreatePinnedToCore(consumer,
                                task_name,
                                1024,
                                NULL,
                                1,
                                NULL,
                                app_cpu);
    }

    // Notify that all tasks have been created
    ESP_LOGI(MAIN_TAG, "All tasks created");

    while (1)
    {
        // Do nothing but allow yielding to lower-priority tasks
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
