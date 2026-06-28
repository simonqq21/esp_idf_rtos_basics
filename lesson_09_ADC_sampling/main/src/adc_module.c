#include "../include/adc_module.h"

extern SemaphoreHandle_t sem_done_reading;
extern TaskHandle_t task_handle_adc_average;
extern TaskHandle_t task_handle_adc_sampling;
extern portMUX_TYPE spinlock;

//
// double buffer
// ****************************************************************
// double buffer for analog readings
int BUFFER_L[ADC_BUFFER_SIZE];
int BUFFER_R[ADC_BUFFER_SIZE];
int *write_to = BUFFER_L;
int *read_from = BUFFER_R;
int idx = 0;
uint8_t buf_overrun = 0;
double adc_avg = 0;

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

// ADC
// ****************************************************************
adc_oneshot_unit_handle_t adc_handle;
/*
calibrate ADC
*/
bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
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
void configure_adc(void)
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
hardware timer ISR that notifies the adc_sampling to read an ADC sample every 100 ms.
*/
void IRAM_ATTR readADCOneShot(void *arg)
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

/*
read analog value from the ADC
*/
void adc_sampling(void *param)
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

/*
task A averages previously collected n samples and writes it to a global floating point variable.
*/
void adc_averaging(void *param)
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
// ****************************************************************