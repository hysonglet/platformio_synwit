
#include <SWM341.h>
#include "synwit_board.h"

volatile static uint32_t g_tick_ms = 0;

#define TEST_MAX        5000

int main(void)
{
    uart0_init();
    SysTick_Config(F_CPU/1000);
    DIV_Init(DIV);

    volatile uint32_t uquo, urem;
	volatile uint32_t squo, srem;

    DIV_UDiv(15, 6);
	while(DIV_Div_IsBusy());
	DIV_UDiv_Result(&uquo, &urem);
	printf("15/6 QUO=%d, REM=%d\r\n", uquo, urem);
	
	DIV_SDiv(-15, 6);
	while(DIV_Div_IsBusy());
	DIV_SDiv_Result(&squo, &srem);
	printf("-15/6 QUO=%d, REM=%d\r\n", squo, srem);
	
	DIV_Root(2, 1);
	while(DIV_Root_IsBusy());
	printf("sqrt(2) = %f\r\n", DIV_Root_Result()/65536.0);

    volatile uint32_t div_tm_start;
    volatile uint32_t div_tm_end;
    div_tm_start = g_tick_ms;
    for (int i = 0; i < TEST_MAX; i++){
        for (int j = 0; j < TEST_MAX; j++){
            DIV_UDiv(i, j);
	        while(DIV_Div_IsBusy());
	        DIV_UDiv_Result(&uquo, &urem);
        }
    }
    div_tm_end = g_tick_ms;
    printf("div  test   time: %lu\n", div_tm_end - div_tm_start);
    volatile uint32_t normal_tm_start;
    volatile uint32_t normal_tm_end;
    normal_tm_start = g_tick_ms;
    for (int i = 0; i < TEST_MAX; i++){
        for (int j = 0; j < TEST_MAX; j++){
            uquo = i/j;
            urem = i%j;
        }
    }
    normal_tm_end = g_tick_ms;
    printf("normal test time: %lu\n", normal_tm_end - normal_tm_start);


    for (;;){
    }
    return 0;
}
void SysTick_Handler(void)
{
    g_tick_ms++;
}