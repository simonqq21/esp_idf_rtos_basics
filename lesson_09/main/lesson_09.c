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
#include "driver/uart.h"
#include "esp_timer.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// semaphores
// ****************************************************************
// indicates when the ADC averaging task is finished reading the double buffer
static SemaphoreHandle_t sem_done_reading = NULL;
// ****************************************************************

// spinlocks
// ****************************************************************
// locks the short critical section where the ESP32 may take multiple cycles computing the average.
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
// ****************************************************************

// task handles
// ****************************************************************
// task handle to the ADC average code that the ADC sampling code notifies after reading
// 10 ADC samples
static TaskHandle_t task_handle_adc_average;
static TaskHandle_t task_handle_adc_sampling;
// ****************************************************************

// queues
// ****************************************************************
// queue for timer ISR to send signal to the ADC sampling code to take one sample.
// static QueueHandle_t adc_evt_queue = NULL;
// ****************************************************************

// double buffer
// ****************************************************************
// double buffer for analog readings
#define ADC_BUFFER_SIZE 10
int BUFFER_L[ADC_BUFFER_SIZE];
int BUFFER_R[ADC_BUFFER_SIZE];
static int *write_to = BUFFER_L;
static int *read_from = BUFFER_R;
int idx = 0;
uint8_t buf_overrun = 0;
static double adc_avg = 0;

/*
swap write_to and read_from pointers in the double buffer
*/
void IRAM_ATTR swap_buffers(void)
{
    int *temp = write_to;
    write_to = read_from;
    read_from = temp;
}
// ****************************************************************

// ISRs
// ****************************************************************
/*
hardware timer ISR that notifies the adc_sampling to read an ADC sample every 100 ms.
*/
static void IRAM_ATTR readADCOneShot(void *arg)
{
    // ESP_LOGI("ADC", "readADCOneShot");
    // // ONLY use this for temporary debugging. It bypasses standard FreeRTOS locks.
    // esp_rom_printf("ISR triggered!\n");
    // ⚠️ Warning: While esp_rom_printf won't instantly crash your system like ESP_LOGI, it still introduces latency. You should remove it as soon as you verify the interrupt is firing.
    // uint8_t i = 1;
    uint32_t arg_val = (uint32_t)arg;
    // uint32_t arg_val = 1;

    // send signal from this ISR to the ADC sampling task.
    // xQueueSendFromISR(adc_evt_queue, &arg_val, NULL);

    // send task notification from this ISR to the ADC sampling task.
    xTaskNotifyGive(task_handle_adc_sampling);
}
// ****************************************************************

// UART
// ****************************************************************
// configure UART
#define UART_PORT_NUM 0
#define UART_BAUD_RATE 115200
#define UART_BUF_SIZE 128
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

/*
task uart_task echoes any character received from the UART back to the same UART.
If the command "avg" is entered, it should display the value of the global average variable.
*/
const char *avg_cmd = "avg";
char rx_buf[UART_BUF_SIZE];
char tx_buf[UART_BUF_SIZE];
static void uart_task(void *param)
{
    while (1)
    {
        // wait for uart characters
        int len = uart_read_bytes(UART_PORT_NUM, rx_buf, UART_BUF_SIZE - 1, 20 / portTICK_PERIOD_MS);
        if (len)
        {
            // print to UART tx buffer
            snprintf(tx_buf, 1024, "CLI_IN: %.1014s", rx_buf);
            // transmit UART tx buffer
            uart_write_bytes(UART_PORT_NUM, tx_buf, strlen(tx_buf));

            // if the command "avg" is entered, print the global average variable.
            tx_buf[0] = '\0';
            if (strncmp(rx_buf, avg_cmd, strlen(avg_cmd)) == 0)
            {
                snprintf(tx_buf, 1024, "%0.2f", adc_avg);
                uart_write_bytes(UART_PORT_NUM, tx_buf, strlen(tx_buf));
                tx_buf[0] = '\0';
            }
            memset(rx_buf, 0, UART_BUF_SIZE);
        }
        // stop task from hogging CPU
        // vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// ****************************************************************

// ADC
// ****************************************************************
#define ADC_INPUT_CHAN ADC_CHANNEL_4
#define ADC_ATTEN ADC_ATTEN_DB_12
adc_oneshot_unit_handle_t adc_handle;
/*
calibrate ADC
*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
#define TAG "ADC_CALIB_TAG"
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Calibration Success");
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated)
    {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    }
    else
    {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

/*
initialize ADC
*/
static void configure_adc(void)
{
    //-------------ADC1 Init---------------//
    adc_oneshot_unit_init_cfg_t adc_init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_init_config, &adc_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_INPUT_CHAN, &config));
    //-------------ADC1 Calibration Init---------------//
    adc_cali_handle_t adc_cali_input_handle = NULL;
    // bool do_calibration_adc = example_adc_calibration_init(ADC_UNIT_1, ADC_INPUT_CHAN, ADC_ATTEN, &adc_cali_input_handle);
    example_adc_calibration_init(ADC_UNIT_1, ADC_INPUT_CHAN, ADC_ATTEN, &adc_cali_input_handle);
}
// /* Stores the handle of the task that will be notified when the
// transmission is complete. */
// static TaskHandle_t xTaskToNotify = NULL;
// /* The index within the target task's array of task notifications
// to use. */
// const UBaseType_t xArrayIndex = 1;

/*
read analog value from the ADC
*/
static void adc_sampling(void *param)
{
    // uint32_t arg_val;
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // if (xQueueReceive(adc_evt_queue, &arg_val, portMAX_DELAY))
        // {
        // 0
        // ESP_LOGI("ADC_TASK", "arg_val = %d", arg_val);
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_INPUT_CHAN, &write_to[idx]));
        // ESP_LOGI("ADC_TASK", "adc val = %ul\n", write_to[0]);
        // ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw[0][0], &voltage[0][0]));

        // increment index
        idx++;

        // if index exceeds ADC buffer size,
        if (idx >= ADC_BUFFER_SIZE)
        {
            // if not done reading yet, it means the buffer has been overrun.
            if (xSemaphoreTake(sem_done_reading, portMAX_DELAY) == pdFALSE)
            {
                buf_overrun = 1;
            }

            // swap buffers and notify task only if overrun flag is cleared.
            if (buf_overrun == 0)
            {
                // reset index
                idx = 0;

                // swap the read pointer and write pointer of the double buffer.
                swap_buffers();

                // signal to averaging task to take average of the last 10 ADC values.
                xTaskNotifyGive(task_handle_adc_average);
            }
        }
        // }
    }
}
// ****************************************************************

/*
task A averages previously collected n samples and writes it to a global floating point variable.
*/
static void adc_averaging(void *param)
{
    // uint32_t ulNotificationValue;
    // const TickType_t xMaxBlockTime = pdMS_TO_TICKS(1000);
    while (1)
    {
        // ulNotificationValue = ulTaskNotifyTakeIndexed(xArrayIndex,
        //                                               pdTRUE, // true for binary semaphore
        //                                               xMaxBlockTime);
        // ulNotificationValue = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // ESP_LOGI("adc_averaging", "%u", ulNotificationValue);
        // once notified by ADC sampling task, take the average.
        // if (ulNotificationValue)
        // {
        // ESP_LOGI("adc_averaging", "averaging_task_handle 1");

        double avg = 0;
        for (int i = 0; i < ADC_BUFFER_SIZE; i++)
        {
            avg += read_from[i];
        }
        avg /= ADC_BUFFER_SIZE;

        if (buf_overrun == 1)
        {
            ESP_LOGI("AVERAGING_TASK", "Buffer overrun, some ADC values dropped.");
        }

        portENTER_CRITICAL(&spinlock);
        adc_avg = avg;
        buf_overrun = 0;
        xSemaphoreGive(sem_done_reading);
        portEXIT_CRITICAL(&spinlock);
        // ESP_LOGI("adc_averaging", "avg = %.2f\n", adc_avg);
        // }

        // else /* The call to ulTaskNotifyTake() timed out. */
        // {
        // ESP_LOGI("adc_averaging", "timeout");
        // }
        // vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

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
    configure_uart();
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
