/*
 * SWM341 + 800x480 RGB LCD + LVGL 9.5 - skeleton lizard demo.
 *
 *   pio run                build
 *   pio run -t upload      flash with probe-rs and watch the RTT output
 *
 * Nothing goes to a UART: logs and bring-up checks are on SEGGER RTT channel 0.
 * Touch the panel to steer the lizard; let go and it roams on its own after
 * 2.6 s. On boards without a GT911 set LIZARD_TOUCH=0 in platformio.ini.
 */
#include <stdio.h>

#include "SWM341.h"
#include "SEGGER_RTT.h"
#include "lvgl.h"

#include "board_lcd.h"
#include "board_touch.h"
#include "lv_port_disp.h"
#include "lizard.h"

#define RTT_CH 0

static void rtt_log_cb(lv_log_level_t level, const char *buf)
{
    (void)level;
    SEGGER_RTT_WriteString(RTT_CH, buf);
}

static void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *label;

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x060b10), 0);

    /* lizard_create() adds the canvas first, so these labels sit on top of it */
    label = lv_label_create(scr);
    lv_label_set_text(label, "SKELETON LIZARD");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xD2FFE6), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 26, 18);

    label = lv_label_create(scr);
    lv_label_set_text(label, "SPECIMEN 07  /  SWM341 + LVGL 9.5");
    lv_obj_set_style_text_color(label, lv_color_hex(0x96DCBE), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 26, 44);

    label = lv_label_create(scr);
    lv_label_set_text(label, "TOUCH THE PANEL - IT FOLLOWS YOU");
    lv_obj_set_style_text_color(label, lv_color_hex(0xA0D2BE), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -24);
}

int main(void)
{
    int sdram;
    int touch;

    SystemInit();            /* 120 MHz from the 12 MHz XTAL */

    board_sdram_init();      /* framebuffers and LVGL heap - first! */
    board_systick_init();    /* 1 ms tick: board_millis() + lv_tick_inc() */

    SEGGER_RTT_Init();
    SEGGER_RTT_printf(RTT_CH, "SWM341 lizard - SYSCLK %u Hz\n", (unsigned)SystemCoreClock);
    SEGGER_RTT_printf(RTT_CH, "LVGL %u.%u.%u\n",
                      (unsigned)LVGL_VERSION_MAJOR,
                      (unsigned)LVGL_VERSION_MINOR,
                      (unsigned)LVGL_VERSION_PATCH);

    /* scribbles over both framebuffers and the heap - do it before lv_init() */
    sdram = board_sdram_test();
    SEGGER_RTT_printf(RTT_CH, "board: %s\n", board_lcd_variant());
    SEGGER_RTT_printf(RTT_CH, "SDRAM: %s\n", sdram == 0 ? "ok" : "FAILED");

    lv_init();
    lv_log_register_print_cb(rtt_log_cb);

    lv_port_disp_init();

#if LIZARD_TOUCH
    touch = board_touch_init();
    SEGGER_RTT_printf(RTT_CH, "touch: %s\n", touch == 0 ? "GT9xx found" : "not found (auto roam)");
#else
    touch = -1;
    (void)touch;
#endif

    if(lizard_create() == NULL) {
        SEGGER_RTT_printf(RTT_CH, "lizard: canvas allocation failed\n");
    }
    ui_create();   /* after the canvas: labels are drawn on top of it */

    board_lcd_init();        /* pin mux + timing + LCD_Start() */

    SEGGER_RTT_printf(RTT_CH, "LCD: BL pin = %d, %u frames in 200 ms\n",
                      board_lcd_bl_level(), (unsigned)board_lcd_frames(200));

    SEGGER_RTT_printf(RTT_CH, "running\n");

    while(1) {
        lv_timer_handler();
    }
}

void SysTick_Handler(void)
{
    board_tick();
    lv_tick_inc(1);
}
