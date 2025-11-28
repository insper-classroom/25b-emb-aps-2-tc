#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
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
const uint PIN_POT_THROTTLE = 26;
const uint PIN_POT_BRAKE = 27;
const int NUM_SAMPLES = 10;
const int ADC_TOLERANCE = 5;

// Paddle shifters
const uint PIN_BTN_UPSHIFT = 2;
const uint PIN_BTN_DOWNSHIFT = 3;

// UART queue
QueueHandle_t xQueueUART;
SemaphoreHandle_t xSemaphore_Upshift;
SemaphoreHandle_t xSemaphore_Downshift;


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
            printf("encoder position %8d, delta %6d\n", new_value, delta);
            last_value = new_value;
            last_delta = delta;
            uart data;
    
            data.control = 0;
            data.val = new_value;
    
            // xQueueSend(xQueueUART, &data, 0);
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

void throttle_potentiometer_task(void *p) {
    uint16_t position_raw;
    uint16_t last_position_raw = 0;

    adc_init();
    adc_gpio_init(PIN_POT_THROTTLE);
    
    while (1) {
        adc_select_input(0);
        position_raw = read_stable_adc();

        uint16_t diff = abs(position_raw - last_position_raw);

        if (diff > ADC_TOLERANCE) {
            printf("throttle position raw: %d\n", position_raw);
            last_position_raw = position_raw;
            
            uart data;

            data.control = 1;
            data.val = position_raw;

            // xQueueSend(xQueueUART, &data, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void brake_potentiometer_task(void *p) {
    uint16_t position_raw;
    uint16_t last_position_raw = 0;

    adc_init();
    adc_gpio_init(PIN_POT_BRAKE);
    
    while (1) {
        adc_select_input(1);
        position_raw = read_stable_adc();

        uint16_t diff = abs(position_raw - last_position_raw);

        if (diff > ADC_TOLERANCE) {
            printf("brake position raw: %d\n", position_raw);
            last_position_raw = position_raw;
            
            uart data;

            data.control = 2;
            data.val = position_raw;

            // xQueueSend(xQueueUART, &data, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void btn_callback(uint gpio, uint32_t events) {
    if (events == 0x4) {
        if (gpio == PIN_BTN_UPSHIFT) {
            xSemaphoreGiveFromISR(xSemaphore_Upshift, 0);
        } else if (gpio == PIN_BTN_DOWNSHIFT) {
            xSemaphoreGiveFromISR(xSemaphore_Downshift, 0);
        }
    }
}

void paddle_shifters_btn_task(void *p) {
    gpio_init(PIN_BTN_UPSHIFT);
    gpio_set_dir(PIN_BTN_UPSHIFT, GPIO_IN);
    gpio_pull_up(PIN_BTN_UPSHIFT);

    gpio_init(PIN_BTN_DOWNSHIFT);
    gpio_set_dir(PIN_BTN_DOWNSHIFT, GPIO_IN);
    gpio_pull_up(PIN_BTN_DOWNSHIFT);

    gpio_set_irq_enabled_with_callback(PIN_BTN_UPSHIFT, GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    gpio_set_irq_enabled(PIN_BTN_DOWNSHIFT, GPIO_IRQ_EDGE_FALL, true);

    while (1) {
        if (xSemaphoreTake(xSemaphore_Upshift, pdMS_TO_TICKS(500))) {
            uart data;

            data.control = 3;
            data.val = 1;
            printf("upshift\n");

            // xQueueSend(xQueueUART, &data, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        if (xSemaphoreTake(xSemaphore_Downshift, pdMS_TO_TICKS(500))) {
            uart data;

            data.control = 4;
            data.val = 1;

            printf("downshift\n");

            // xQueueSend(xQueueUART, &data, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
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

    xQueueUART = xQueueCreate(4, sizeof(uart));
    xSemaphore_Upshift = xSemaphoreCreateBinary();
    xSemaphore_Downshift = xSemaphoreCreateBinary();

    xTaskCreate(encoder_task, "Encoder Task", 8192, NULL, 1, NULL);
    xTaskCreate(paddle_shifters_btn_task, "Paddle Shifters Btn Task", 8192, NULL, 1, NULL);
    // xTaskCreate(throttle_potentiometer_task, "Throttle Potentiometer Task", 8192, NULL, 1, NULL);
    // xTaskCreate(brake_potentiometer_task, "Brake Potentiometer Task", 8192, NULL, 1, NULL);
    xTaskCreate(uart_task, "UART Task", 8192, NULL, 1, NULL);

    vTaskStartScheduler();

    while(true);
}