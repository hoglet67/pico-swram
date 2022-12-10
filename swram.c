#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "swram.pio.h"

#include "swram.h"
#include "adt_rom.h"

static PIO pio = pio1;

volatile uint8_t memory[0x4000] __attribute__((aligned(0x4000)))  = "Pi Pico says 'hello' to Acorn Electron!";            // Sideway RAM/ROM area

//void __no_inline_not_in_flash_func(main_loop())
//{
//   u_int8_t data;
//   u_int16_t address;
//   while (true)
//      {
//         // Get event from SM 0
//         u_int32_t reg = pio_sm_get_blocking(pio, 0);
//         address = reg & 0x00FF;     // voor nu alleen lage nibble omdat ik maar 8 bits adres heb
//
//         if (!(reg & 0x1000000)) {
//            // read address
//            data = memory[address];
//            pio_sm_put(pio, 1, 0xFF00 | data);
//            //            printf("Data transmitted: %04X => %02X\n", address, data);
//         } else {
//            data = (reg & 0xFF0000) >> 16;
//            memory[address] = data;
//            //            printf("Data received: %07X, %04X => %02X\n", reg, address, data);
//         }
//      }
//}


// Chained DMA courtesy of Andrew Gordon (arg)
// https://github.com/arg08/picoeco/blob/main/ecotest/bus1mhz.c#L60
//
// Set up a pair of DMAs to serve one instance of the read SM
// First DMA is 32-wide, DREQ from the SM's RxFIFO,
// source RxFIFO, dest 2nd DMA's source-plus-trigger register.
// Second DMA is 8-wide, no DREQ, count 1, dest SM's TxFIFO.
// We trigger DMA1 now and it sits waiting for DREQ; DMA2 gets
// triggered each time by DMA1.
// Ideally, DMA1 would have infinite transfer count and DMA2
// transfer count of 1.  Unfortunately, the hardware doesn't allow an
// infinite count, and 0xffffffff isn't close enough to infinity
// (2^32 transfers at 1MHz would be 4000 seconds; BBC can't actually do
// back-to-back transfers, but could potentially do that many in
// a couple of days).
// So instead we set up both units with transfer count of 1,
// and get DMA2 to re-trigger DMA1 on completion (via the chain trigger).
static void setup_dma(unsigned sm)
{
	unsigned dma1, dma2;
	dma_channel_config cfg;

	dma1 = dma_claim_unused_channel(true);
	dma2 = dma_claim_unused_channel(true);
   //	printf("Using DMA%u, DMA%u for SM %u\n", dma1, dma2, sm);

	// Set up DMA2 first (it's not triggered until DMA1 does so)
	cfg = dma_channel_get_default_config(dma2);
	channel_config_set_read_increment(&cfg, false);
		// write increment defaults to false
		// dreq defaults to DREQ_FORCE
	channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
	channel_config_set_chain_to(&cfg, dma1);
	dma_channel_set_write_addr(dma2, &(pio->txf[sm]), false);
	dma_channel_set_trans_count(dma2, 1, false);
	dma_channel_set_config(dma2, &cfg, false);

	// Set up DMA1 and trigger it
	cfg = dma_channel_get_default_config(dma1);
	channel_config_set_read_increment(&cfg, false);
		// write increment defaults to false
	channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, false));
		// transfer size defaults to 32
	dma_channel_set_trans_count(dma1, 1, false);
	dma_channel_set_read_addr(dma1, &(pio->rxf[sm]), false);
	dma_channel_set_write_addr(dma1, &(dma_hw->ch[dma2].al3_read_addr_trig),
		false);
	dma_channel_set_config(dma1, &cfg, true);
}

int main() {

   // The system clock speed is set as a constant in the PIO file
   set_sys_clock_khz(SYSCLK_MHZ * 1000, true);

   // Copy the ADT ROM
   memcpy(memory, adt_rom, 0x4000);

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

   // Setup state machine 0, which handles memory access
   uint offset = pio_add_program(pio, &access_ram_program);
   access_ram_program_init(pio, 0, offset);
   pio_sm_set_enabled(pio, 0, true);

   // Set the X register in SM0 to the memory base
   pio_sm_put_blocking(pio, 0, ((unsigned) memory) >> 14);
   pio_sm_exec_wait_blocking(pio, 0, pio_encode_pull(false, false));
   pio_sm_exec_wait_blocking(pio, 0, pio_encode_mov(pio_x, pio_osr));

   // Setup the chain DMA for state machine 0
   setup_dma(0);

   // Setip state machne 1, which samples nOE
   offset = pio_add_program(pio, &sample_noe_program);
   sample_noe_program_init(pio, 1, offset);
   pio_sm_set_enabled(pio, 1, true);


   stdio_init_all();

   // main_loop();

   while (true) {
      tight_loop_contents();
   }
}
