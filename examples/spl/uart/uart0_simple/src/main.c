
#include <SWM341.h>
#include "synwit_board.h"
#include <stdio.h>

int main(void)
{
    uint32_t cnt = 0;
    uart0_init();
    for (;;)
    {
        delay_ms(1000);
        printf("%s %d\n", "Hello Synwit, I'm Hysonglet.", cnt++);
    }
    return 0;
}
