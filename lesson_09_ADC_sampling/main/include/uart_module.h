#ifndef UART_MODULE_H
#define UART_MODULE_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"

// configure UART
#define UART_PORT_NUM 0
#define UART_BAUD_RATE 115200
#define UART_BUF_SIZE 128

void uart_configure(void);
void uart_task(void *param);

#endif