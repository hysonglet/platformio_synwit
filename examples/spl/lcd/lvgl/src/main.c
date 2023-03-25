
#include "synwit_board.h"
#include "system.h"
#include "lvgl.h"

#include "lv_porting/lv_port_disp.h"
#include "lv_porting/lv_port_indev.h"

/* ugui 定时刷新间隔  */
#define UG_UPDATE_INTVAL_MS     30

void lvgl_task(void)
{
    lv_timer_handler();
}

#include "lv_demos.h"

int main(void)
{
    SysTick_Config(F_CPU/1000);
    uart0_init();
    sdram_init();
    delay_ms(10);

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

#if LV_USE_DEMO_WIDGETS
    lv_demo_widgets();
#endif
#if LV_USE_DEMO_BENCHMARK
    lv_demo_benchmark();
#endif
#if LV_USE_DEMO_STRESS
    lv_demo_stress();
#endif

    for (;;){
        lvgl_task();
    }
    return 0;
}


static uint32_t s_tick = 0;

void SysTick_Handler(void)
{
    s_tick++;
    lv_tick_inc(1);
}
