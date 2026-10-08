/*
 * Synwit SWM341 - SEGGER RTT console.
 *
 * Prints to RTT channel 0 instead of a UART: no pins, no wiring, output shows
 * up in J-Link RTT Viewer / RTT Console as soon as the probe is attached.
 * The LED on PA5 keeps blinking so you can tell the chip is alive.
 *
 *   pio run -t upload
 *   JLinkRTTViewer / JLinkRTTLogger   (RTT channel 0)
 *
 * _write() at the bottom routes plain printf() to RTT as well: PlatformIO only
 * unpacks the RTT/ subdirectory of SEGGER's repo, so the bundled
 * Syscalls/SEGGER_RTT_Syscalls_GCC.c is not compiled in - we do it ourselves.
 */
#include <stdio.h>
#include "SWM341.h"
#include "SEGGER_RTT.h"

#define LED_PORT GPIOA
#define LED_PIN  PIN5

int main(void)
{
    SystemInit();

    SEGGER_RTT_Init();
    setvbuf(stdout, NULL, _IONBF, 0); /* no buffering: printf() goes out at once */

    GPIO_Init(LED_PORT, LED_PIN, 1, 0, 0, 0); /* output, drives the LED */

    SEGGER_RTT_printf(0, "Hi from SWM341 over RTT! SYSCLK = %u Hz\n", SystemCoreClock);

    for (uint32_t n = 0;; n++)
    {
        GPIO_InvBit(LED_PORT, LED_PIN);

        SEGGER_RTT_printf(0, "tick %u\n", n);
        printf("printf() also goes to RTT\n");

        for (uint32_t i = 0; i < SystemCoreClock / 8; i++)
            __NOP();
    }
}

/* printf() ends up here - overrides the libnosys stub */
int _write(int fd, char *ptr, int len)
{
    (void) fd;

    return (int) SEGGER_RTT_Write(0, ptr, len);
}
