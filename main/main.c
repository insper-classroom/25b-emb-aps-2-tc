#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "pico_emb.pio.h"

// Base pin to connect the A phase of the encoder.
// The B phase must be connected to the next pin
const uint PIN_AB = 16;

// Potentiometer
const uint PIN_POT = 26;
const int NUM_SAMPLES = 10;
const int ADC_TOLERANCE = 5;

// UART queue
QueueHandle_t xQueueUART;

typedef struct uart {
    int control;
    int val;
} uart;

void encoder_task(void *p) {
    int new_value, delta, old_value = 0;
    int last_value = -1, last_delta = -1;

    stdio_init_all();

    PIO pio = pio0;
    const uint sm = 0;

    pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm, PIN_AB, 0);

    while (1) {
        new_value = quadrature_encoder_get_count(pio, sm);
        delta = new_value - old_value;
        old_value = new_value;

        if (new_value != last_value || delta != last_delta ) {
            // printf("position %8d, delta %6d\n", new_value, delta);
            last_value = new_value;
            last_delta = delta;
            uart data;
    
            data.control = 0;
            data.val = new_value;
    
            xQueueSend(xQueueUART, &data, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static uint16_t read_stable_adc() {
    uint32_t total = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        total += adc_read();
        // busy_wait_us(100);
    }
    return (uint16_t)(total / NUM_SAMPLES);
}

void potentiometer_task(void *p) {
    uint16_t position_raw;
    uint16_t last_position_raw = 0;

    adc_init();
    adc_gpio_init(PIN_POT);
    
    while (1) {
        adc_select_input(0);
        position_raw = read_stable_adc();

        uint16_t diff = abs(position_raw - last_position_raw);

        if (diff > ADC_TOLERANCE) {
            last_position_raw = position_raw;
            
            uart data;

            data.control = 1;
            data.val = position_raw;

            xQueueSend(xQueueUART, &data, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void uart_task(void *p) {
    uart data;

    while (1) {
        if (xQueueReceive(xQueueUART, &data, pdMS_TO_TICKS(100))) {
            uart_putc_raw(uart0, -1);
            uart_putc_raw(uart0, data.control);
            uart_putc_raw(uart0, data.val);
            uart_putc_raw(uart0, data.val >> 8);
        }        
    }
}

int main() {
    stdio_init_all();

    xQueueUART = xQueueCreate(3, sizeof(uart));

    xTaskCreate(encoder_task, "Encoder Task", 8192, NULL, 1, NULL);
    xTaskCreate(potentiometer_task, "Potentiometer Task", 8192, NULL, 1, NULL);
    xTaskCreate(uart_task, "UART Task", 8192, NULL, 1, NULL);

    vTaskStartScheduler();

    while(true);
}