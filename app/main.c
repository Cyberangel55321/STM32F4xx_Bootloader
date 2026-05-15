#include <stdint.h>
#include "stm32f4xx.h"
#include "board.h"
#include "bl_usart.h"
#include "console.h"

#define APP_BASE_ADDR 0x08010000

extern void board_lowlevel_init(void);
extern void bootloader_main(void);

int main(void)
{
	board_lowlevel_init();
    console_init();

    bootloader_main();

	extern void JumpApp(uint32_t app_addr);
	JumpApp(APP_BASE_ADDR);

	return 0;
}


