#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "swram.pio.h"

#include "swram.h"

static PIO pio = pio1;

volatile uint8_t memory[0x4000] = "Pi Pico says 'hello' to Acorn Electron!";            // Sideway RAM/ROM area

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

   // The system clock speed is set as a constant in the PIO file
   set_sys_clock_khz(SYSCLK_MHZ * 1000, true);

   // The AD pins are bidirectional, so the need initializing
   for (uint pin = PIN_AD_BASE; pin < PIN_AD_BASE + 8; pin++) {
      gpio_init(pin);
      gpio_set_dir(pin, false);
      gpio_set_function(pin, GPIO_FUNC_PIO1);
   }

   // Output enable for the 74lvc245 buffers
   gpio_pull_up(PIN_SELAL);
   gpio_pull_up(PIN_SELAH);
   gpio_pull_up(PIN_SELDT);
   gpio_set_function(PIN_SELAL, GPIO_FUNC_PIO1);
   gpio_set_function(PIN_SELAH, GPIO_FUNC_PIO1);
   gpio_set_function(PIN_SELDT, GPIO_FUNC_PIO1);

   uint offset = pio_add_program(pio, &access_ram_program);
   access_ram_program_init(pio, 0, offset);
   pio_sm_set_enabled(pio, 0, true);

   offset = pio_add_program(pio, &sample_noe_program);
   sample_noe_program_init(pio, 1, offset);
   pio_sm_set_enabled(pio, 1, true);

   stdio_init_all();

   main_loop();
}
