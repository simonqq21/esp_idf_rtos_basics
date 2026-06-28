#include "../include/uart_module.h"

// UART
// ****************************************************************

extern double adc_avg;

void uart_configure(void)
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
void uart_task(void *param)
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
