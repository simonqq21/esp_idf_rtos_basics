#ifndef ADC_MODULE_H
#define ADC_MODULE_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define TAG "ADC_CALIB_TAG"
#define ADC_INPUT_CHAN ADC_CHANNEL_4
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_BUFFER_SIZE 10

void swap_buffers(void);
bool example_adc_calibration_init(adc_unit_t unit,
                                  adc_channel_t channel,
                                  adc_atten_t atten,
                                  adc_cali_handle_t *out_handle);
void configure_adc(void);
void readADCOneShot(void *arg);
void adc_sampling(void *param);
void adc_averaging(void *param);

#endif
