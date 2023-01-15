#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "swram.pio.h"

#include "swram.h"
#include "adt_rom.h"

#define SMC_WRITE     0
#define SMC_READ      1
#define SMC_DEGLITCH  2
#define SMC_RNW_INV   3

static PIO pio = pio1;

volatile uint8_t memory[0x8000] __attribute__((aligned(0x8000)))  = "Pi Pico says 'hello' to Acorn Electron!";            // Sideway RAM/ROM area

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
static void setup_read_dma(unsigned sm)
{
   unsigned dma1, dma2;
   dma_channel_config cfg;

   dma1 = dma_claim_unused_channel(true);
   dma2 = dma_claim_unused_channel(true);

   // Set up DMA2 first (it's not triggered until DMA1 does so)
   cfg = dma_channel_get_default_config(dma2);
   channel_config_set_read_increment(&cfg, false);
   // write increment defaults to false
   // dreq defaults to DREQ_FORCE
   channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
   dma_channel_set_trans_count(dma2, 1, false);
   dma_channel_set_write_addr(dma2, &(pio->txf[sm]), false);
   channel_config_set_chain_to(&cfg, dma1);
   dma_channel_set_config(dma2, &cfg, false);

   // Set up DMA1 and trigger it
   cfg = dma_channel_get_default_config(dma1);
   channel_config_set_read_increment(&cfg, false);
   // write increment defaults to false
   channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, false));
   // transfer size defaults to 32
   dma_channel_set_trans_count(dma1, 1, false);
   dma_channel_set_read_addr(dma1, &(pio->rxf[sm]), false);
   dma_channel_set_write_addr(dma1, &(dma_hw->ch[dma2].al3_read_addr_trig), false);
   dma_channel_set_config(dma1, &cfg, true);
}

static void setup_write_dma(unsigned sm)
{
   unsigned dma1, dma2;
   dma_channel_config cfg;

   dma1 = dma_claim_unused_channel(true);
   dma2 = dma_claim_unused_channel(true);

   // Set up DMA2 first (it's not triggered until DMA1 does so)
   cfg = dma_channel_get_default_config(dma2);
   channel_config_set_read_increment(&cfg, false);
   // write increment defaults to false
   channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, false));
   channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
   channel_config_set_chain_to(&cfg, dma1);
   dma_channel_set_read_addr(dma2, &(pio->rxf[sm]), false);
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
   dma_channel_set_write_addr(dma1, &(dma_hw->ch[dma2].al2_write_addr_trig), false);
   dma_channel_set_config(dma1, &cfg, true);
}

static void set_x(unsigned smc, unsigned x) {
   pio_sm_put_blocking(pio, smc, x);
   pio_sm_exec_wait_blocking(pio, smc, pio_encode_pull(false, false));
   pio_sm_exec_wait_blocking(pio, smc, pio_encode_mov(pio_x, pio_osr));
}

int main() {

   // The system clock speed is set as a constant in the PIO file
   set_sys_clock_khz(SYSCLK_MHZ * 1000, true);

   // Copy the ADT ROM to ROM 1
   memcpy((void *)memory + 0x4000, adt_rom, 0x4000);

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

   // Setup an additional state machine to deglitch Phi0 onto a PIO interrupt
   uint offset = pio_add_program(pio, &deglitch_phi0_program);
   deglitch_phi0_program_init(pio, SMC_DEGLITCH, offset);
   pio_sm_set_enabled(pio, SMC_DEGLITCH, true);

   // Setup another additional state machine to invert RnW onto PIN_RNW_INV
   offset = pio_add_program(pio, &invert_rnw_program);
   invert_rnw_program_init(pio, SMC_RNW_INV, offset);
   pio_sm_set_enabled(pio, SMC_RNW_INV, true);

   // Load the access_ram program that's shared by the read and write state machines
   offset = pio_add_program(pio, &access_ram_program);

   // Initialize the read state machine (handles read accesses)
   access_ram_program_init(pio, SMC_READ, offset, 0);
   pio_sm_set_enabled(pio, SMC_READ, true);

   // Initialize the write state machine (handles write accesses)
   access_ram_program_init(pio, SMC_WRITE, offset, 1);
   pio_sm_set_enabled(pio, SMC_WRITE, true);

   // Set the X register in both state machines to the memory base bits 31..15
   set_x(SMC_READ,  ((unsigned) memory) >> 15);
   set_x(SMC_WRITE, ((unsigned) memory) >> 15);

   // Setup the DMA chain for state machine 0
   setup_read_dma(SMC_READ);

   // Setup the DMA chain for state machine 1
   setup_write_dma(SMC_WRITE);

   stdio_init_all();

   // main_loop();

   while (true) {
      tight_loop_contents();
   }
}
