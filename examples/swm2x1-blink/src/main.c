/*
 * Synwit SWM2X1 - blink the LED on PA5 and print over UART0 (57600 8N1).
 *
 * Wiring (identical to the vendor SimplUART / KeyLED samples):
 *   LED  : PA5
 *   UART0: TX=PA1  RX=PA0
 *
 *   pio run -e <board id>          build for one chip
 *   pio run -t upload              flash (needs a working upload_command)
 *
 * printf() is routed to UART0 by _write() below: the platform links
 * -specs=nosys.specs, so the default _write is just a weak stub that
 * swallows everything.
 */
#include <stdio.h>
#include "SWM2X1.h"

#define LED_PORT GPIOA
#define LED_PIN  PIN5

/* SWM201 和 SWM211 共用一份 CSL，靠导入脚本加的 CHIP_ 宏区分 */
#if defined(CHIP_SWM201)
#define CHIP_NAME "SWM201"
#elif defined(CHIP_SWM211)
#define CHIP_NAME "SWM211"
#else
#define CHIP_NAME "SWM2X1"
#endif

void SerialInit(void);

int main(void)
{
	SystemInit();

	SerialInit();
	setvbuf(stdout, NULL, _IONBF, 0);	/* no buffering: printf() goes out at once */

	GPIO_Init(GPIOA, PIN5, 1, 0, 0, 0);				/* output, drives the LED */

	printf("Hi from %s! SYSCLK = %u Hz\n", CHIP_NAME, SystemCoreClock);

	while (1)
	{
		GPIO_InvBit(LED_PORT, LED_PIN);

		for (uint32_t i = 0; i < SystemCoreClock / 8; i++)
			__NOP();
	}
}

void SerialInit(void)
{
	UART_InitStructure UART_initStruct;

	PORT_Init(PORTA, PIN0, PORTA_PIN0_UART0_RX, 1);
	PORT_Init(PORTA, PIN1, PORTA_PIN1_UART0_TX, 0);

	UART_initStruct.Baudrate = 57600;
	UART_initStruct.DataBits = UART_DATA_8BIT;
	UART_initStruct.Parity = UART_PARITY_NONE;
	UART_initStruct.StopBits = UART_STOP_1BIT;
	UART_initStruct.RXThreshold = 3;
	UART_initStruct.RXThresholdIEn = 0;
	UART_initStruct.TXThreshold = 3;
	UART_initStruct.TXThresholdIEn = 0;
	UART_initStruct.TimeoutTime = 10;
	UART_initStruct.TimeoutIEn = 0;
	UART_Init(UART0, &UART_initStruct);
	UART_Open(UART0);
}

/* printf() ends up here - overrides the libnosys stub */
int _write(int fd, char *ptr, int len)
{
	(void) fd;

	for (int i = 0; i < len; i++)
	{
		UART_WriteByte(UART0, ptr[i]);

		while (UART_IsTXBusy(UART0));
	}

	return len;
}
