#include <SWM341.h>
#include <synwit_board.h>


int main()
{
    led_init();
    /* 1ms中断一次 */
    SysTick_Config(F_CPU/1000);
    
    while(1);
    return 0;
}

void SysTick_Handler(void)
{	
    static int cnt = 0;
	/* 1秒翻转一次 */
    if(++cnt == 1000){
        cnt = 0;
        led_turn();
    }
}
