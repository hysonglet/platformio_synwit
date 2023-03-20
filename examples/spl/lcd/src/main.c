
#include <SWM341.h>
#include "synwit_board.h"
#include <stdio.h>
#include "ugui.h"

/* ugui 定时刷新间隔  */
#define UG_UPDATE_INTVAL_MS     30

#define LCD_BUFFER              (uint32_t *)SDRAMM_BASE
#define LCD_WIDTH               480
#define LCD_HEIGHT              272

/* 选择是否软件旋转屏幕 */
#define LCD_ROTATION            false

static void lcd_draw_pix(uint16_t x, uint16_t y, UG_COLOR color)
{
    /* lcd format: 565 */
    uint16_t *lcd_buf = (uint16_t *)LCD_BUFFER;
#if LCD_ROTATION == true
    lcd_buf[(LCD_HEIGHT - x) * LCD_WIDTH + y] = (uint16_t)color;
#else
    lcd_buf[(y * LCD_WIDTH) + x] = (uint16_t)color;
#endif
}

UG_GUI g_gui;
UG_WINDOW g_win;
UG_OBJECT g_obj_win_list[10];
UG_BUTTON g_btn_left;
UG_BUTTON g_btn_right;
UG_TEXTBOX g_txtbox;

static void win_call_back(UG_MESSAGE *msg)
{

}


static bool s_ug_update_flag = false;

static uint64_t s_tick = 0;

int main(void)
{
    SysTick_Config(F_CPU/1000);
    uart0_init();
    sdram_init();
    rgb_lcd_init(LCD_BUFFER, 480, 272);

#if LCD_ROTATION == true
	UG_Init(&g_gui,(void(*)(UG_S16,UG_S16,UG_COLOR))lcd_draw_pix, LCD_HEIGHT, LCD_WIDTH);
#else
	UG_Init(&g_gui,(void(*)(UG_S16,UG_S16,UG_COLOR))lcd_draw_pix, LCD_WIDTH, LCD_HEIGHT);
#endif

    // NVIC_SetPriorityGrouping(2);
    // NVIC_SetPriority(LCD_IRQn, 0);

    UG_FillScreen(C_BLACK);

    // UG_FillCircle(LCD_WIDTH/2, LCD_HEIGHT/2, 50, C_BLACK);
    // UG_FontSelect(&FONT_24X40);
    // UG_SetBackcolor(C_BLACK);
    // UG_SetForecolor(C_CYAN);
    // UG_PutString(0, 0, "hello world");
	/* 创建窗口  */
    UG_WindowCreate(&g_win, g_obj_win_list, 10, win_call_back);
    UG_WindowSetTitleTextFont(&g_win, &FONT_24X40);
    UG_WindowSetBackColor(&g_win, C_BLACK);
    UG_WindowSetForeColor(&g_win, C_RED);
    UG_WindowSetTitleText(&g_win, "WIN");
    UG_WindowSetTitleColor(&g_win, C_BLUE);
	/* 创建两个按键  */
    UG_ButtonCreate(&g_win, &g_btn_left, BTN_ID_0, 10, 10, 110, 60);
    UG_ButtonCreate(&g_win, &g_btn_right, BTN_ID_1, 10, 150, 110, 200);
	/* 创建文本  */
    UG_TextboxCreate(&g_win, &g_txtbox, TXB_ID_0, 
        UG_GetXDim()/2, UG_GetYDim()/2, 
        UG_GetXDim()/2 + 100, UG_GetYDim()/2 + 50);

    UG_ButtonSetFont(&g_win, BTN_ID_0, &FONT_12X16);
    UG_ButtonSetFont(&g_win, BTN_ID_1, &FONT_12X16);
    UG_ButtonSetText(&g_win, BTN_ID_0, "OK");
    UG_ButtonSetText(&g_win, BTN_ID_1, "Button");
    UG_TextboxSetFont(&g_win, TXB_ID_0, &FONT_8X12);
    UG_TextboxSetForeColor(&g_win, TXB_ID_0, C_RED);

    UG_WindowShow(&g_win);
    
    for (;;){
		/* 每隔1s更新文本框的数字 */
        if(s_tick % 1000 == 0){
            char num_str[10];
            snprintf(num_str, sizeof(num_str) - 1, "%d", (int)s_tick/1000);
            UG_TextboxSetText(&g_win, TXB_ID_0, num_str);
        }

        /* 30ms刷新一次 */
        if(s_ug_update_flag == true){
            s_ug_update_flag = false;
            UG_Update();
        }
    }
    return 0;
}



void SysTick_Handler(void)
{
    s_tick++;
	/* 定时刷新屏幕缓冲的内容 */
    if((s_tick % UG_UPDATE_INTVAL_MS) == 0){
        s_ug_update_flag = true;
    }
}
