#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
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
const float CONVERSION_FACTOR = 3.3f / 4095.0f;
const int NUM_SAMPLES = 10;


void encoder_task(void *p) {
    int new_value, delta, old_value = 0;
    int last_value = -1, last_delta = -1;

    stdio_init_all();
    printf("Hello from quadrature encoder\n");

    PIO pio = pio0;
    const uint sm = 0;

    pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm, PIN_AB, 0);

    while (1) {
        new_value = quadrature_encoder_get_count(pio, sm) / 2.222;
        delta = new_value - old_value;
        old_value = new_value;

        if (new_value != last_value || delta != last_delta ) {
            printf("position %8d, delta %6d\n", new_value, delta);
            last_value = new_value;
            last_delta = delta;
        }
        sleep_ms(100);
    }
}

static uint16_t read_stable_adc() {
    uint32_t total = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        total += adc_read();
        vTaskDelay(pdMS_TO_TICKS(50));
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

        if (position_raw != last_position_raw) {            
            printf("Posição Bruta: %u\n", position_raw); 
            last_position_raw = position_raw;
            
            // 
            // É ESTE VALOR 'position_raw' (0-4095) que você deve
            // calibrar (min/max) e enviar para o PC via USB HID.
            //
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

int main() {
ç    stdio_init_all();

    xTaskCreate(encoder_task, "Encoder Task", 8192, NULL, 1, NULL);
    xTaskCreate(potentiometer_task, "Potentiometer Task", 8192, NULL, 1, NULL);

    vTaskStartScheduler();

    while(true);
}