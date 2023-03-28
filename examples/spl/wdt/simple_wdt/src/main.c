
#include <SWM341.h>
#include "synwit_board.h"
#include <stdio.h>

int main(void)
{
    uart0_init();
	
	/* 查看复位源 */
    if(SYS->RSTSR & SYS_RSTSR_WDT_Msk){
		SYS->RSTSR = (1 << SYS_RSTSR_WDT_Pos);
		printf("WDT Reset Happened\r\n");
	}

    /* 设置为 1s 一次喂狗 */
    WDT_Init(WDT, 0, 1024);
	WDT_Start(WDT);
	int cy = 100;
    for (;;){
        delay_ms(cy += 100);
        WDT_Feed(WDT);
    }
    return 0;
}
