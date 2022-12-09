/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "elkread.pio.h"
#include "elkwrite.pio.h"

#include "swram.h"

void __no_inline_not_in_flash_func(main_loop())
{
    u_int8_t data;
    u_int16_t address;
    while (true)
    {
        // Get event from SM 0
        u_int32_t reg = pio_sm_get_blocking(pio, 0);
        address = reg & 0x00FF;     // voor nu alleen lage nibble omdat ik maar 8 bits adres heb

        if (!(reg & 0x1000000)) {
            // read address
            data = memory[address];
            pio_sm_put(pio, 1, 0xFF00 | data);
//            printf("Data transmitted: %04X => %02X\n", address, data);
        } else {
            data = (reg & 0xFF0000) >> 16;
            memory[address] = data;
//            printf("Data received: %07X, %04X => %02X\n", reg, address, data);
        }
    }
}


int main() {
    // pins 2 to 9 are used to read the 6502, 8 bits at a time
    for (uint pin = 2; pin <= 9; pin++)
    {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_set_function(pin, GPIO_FUNC_PIO1);
    }

    // Output enable for the 74lvc245 buffers
    gpio_pull_up(GPIO_PIN_SELAL);
    gpio_pull_up(GPIO_PIN_SELAH);
    gpio_pull_up(GPIO_PIN_SELDT);

    gpio_set_function(GPIO_PIN_SELAL, GPIO_FUNC_PIO1);
    gpio_set_function(GPIO_PIN_SELAH, GPIO_FUNC_PIO1);
    gpio_set_function(GPIO_PIN_SELDT, GPIO_FUNC_PIO1);

    uint offset = pio_add_program(pio, &elkread_program);
    elkread_program_init(pio, 0, offset);
    pio_sm_set_enabled(pio, 0, true);

    offset = pio_add_program(pio, &elkwrite_program);
    elkwrite_program_init(pio, 1, offset);
    pio_sm_set_enabled(pio, 1, true);

    stdio_init_all();

    main_loop();
}

