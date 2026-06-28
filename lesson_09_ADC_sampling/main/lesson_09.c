#include <stdio.h>

/*
Your challenge is to create a sampling, processing, and interface system using
hardware interrupts and whatever kernel objects (e.g. queues, mutexes, and
semaphores) you might need.

You should implement a hardware timer in the ESP32 (here is a good article showing
how to do that) that samples from an ADC pin once every 100 ms. This sampled data
should be copied to a double buffer (you could also use a circular buffer). Whenever
one of the buffers is full, the ISR should notify Task A.

Task A, when it receives notification from the ISR, should wake up and compute the
average of the previously collected 10 samples. Note that during this calculation time,
the ISR may trigger again. This is where a double (or circular) buffer will help: you
can process one buffer while the other is filling up.

When Task A is finished, it should update a global floating point variable that contains
the newly computed average. Do not assume that writing to this floating point variable
will take a single instruction cycle! You will need to protect that action as we saw in
the queue episode.

Task B should echo any characters received over the serial port back to the same serial
port. If the command “avg” is entered, it should display whatever is in the global
average variable.

This is like a “final project” in that you will need to use many of the concepts we
covered in previous lectures and challenges.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "soc/soc_caps.h"

#include "driver/gpio.h"
#include "esp_timer.h"

#include "sdkconfig.h"

#include "include/adc_module.h"
#include "include/uart_module.h"

extern double adc_avg;

// semaphores
// ****************************************************************
// indicates when the ADC averaging task is finished reading the double buffer
SemaphoreHandle_t sem_done_reading = NULL;
// ****************************************************************

// spinlocks
// ****************************************************************
// locks the short critical section where the ESP32 may take multiple cycles computing the average.
portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
// ****************************************************************

// task handles
// ****************************************************************
// task handle to the ADC average code that the ADC sampling code notifies after reading
// 10 ADC samples
TaskHandle_t task_handle_adc_average;
TaskHandle_t task_handle_adc_sampling;
// ****************************************************************

// queues
// ****************************************************************
// queue for timer ISR to send signal to the ADC sampling code to take one sample.
// static QueueHandle_t adc_evt_queue = NULL;
// ****************************************************************

// ESP timer
// ****************************************************************
/*
configure esp_timer
*/
esp_timer_handle_t ADC_timer_handle;
static void configure_start_timer(void)
{
    // set CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD enabled in menuconfig
    // Component config > ESP Timer (High Resolution Timer)
    const esp_timer_create_args_t timer_args = {
        .callback = &readADCOneShot,
        .name = "readADCOneShot",
        .dispatch_method = ESP_TIMER_ISR,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ADC_timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(ADC_timer_handle, 100 * 1000)); // 100 ms
}
// ****************************************************************

void app_main(void)
{
    // adc_evt_queue = xQueueCreate(3, sizeof(uint32_t));
    configure_adc();
    uart_configure();
    configure_start_timer();

    // create semaphores
    sem_done_reading = xSemaphoreCreateBinary();
    // restart if semaphore couldn't be created
    if (sem_done_reading == NULL)
    {
        ESP_LOGE("APP_MAIN", "cannot create semaphores, restarting.");
        esp_restart();
    }
    // give semaphore to set it to 1.
    xSemaphoreGive(sem_done_reading);

    xTaskCreatePinnedToCore(adc_sampling,
                            "ADC sampling task",
                            1024,
                            NULL,
                            1,
                            &task_handle_adc_sampling,
                            0);

    xTaskCreatePinnedToCore(adc_averaging,
                            "averaging task",
                            1024,
                            NULL,
                            1,
                            &task_handle_adc_average,
                            0);

    xTaskCreatePinnedToCore(uart_task,
                            "UART task",
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
