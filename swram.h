#define GPIO_PIN_AD0 2
#define GPIO_PIN_AD1 3
#define GPIO_PIN_AD2 4
#define GPIO_PIN_AD3 5
#define GPIO_PIN_AD4 6
#define GPIO_PIN_AD5 7
#define GPIO_PIN_AD6 8
#define GPIO_PIN_AD7 9

#define GPIO_PIN_O0 18
#define GPIO_PIN_RW 19
#define GPIO_PIN_OE 15

#define GPIO_PIN_SELAL 28
#define GPIO_PIN_SELAH 27
#define GPIO_PIN_SELDT 26

static PIO pio = pio1;

volatile uint8_t memory[0x4000] = "Pi Pico says 'hello' to Acorn Electron!";            // Sideway RAM/ROM area