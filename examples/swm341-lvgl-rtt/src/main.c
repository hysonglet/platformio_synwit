/*
 * SWM341 + 800x480 RGB LCD + LVGL, logging over SEGGER RTT.
 *
 *   pio run                build
 *   pio run -t upload      flash with probe-rs and watch the RTT output
 *   JLinkRTTViewer / `probe-rs attach` on a second console also work
 *
 * Nothing is printed on a UART: the log goes through the debug probe, so the
 * only wiring needed is the SWD connector and the LCD ribbon.
 */
#include <stdio.h>

#include "SWM341.h"
#include "SEGGER_RTT.h"
#include "lvgl.h"

#include "board_lcd.h"
#include "lv_port_disp.h"

#define RTT_CH 0

static lv_obj_t *s_lbl_time;
static lv_obj_t *s_bar;
static lv_obj_t *s_lbl_info;

static uint32_t s_loops;
static uint32_t s_period_start;

/* LV_USE_LOG is on: everything LVGL complains about goes to RTT channel 0 */
static void rtt_log_cb(const char *buf)
{
    SEGGER_RTT_WriteString(RTT_CH, buf);
}

static void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *title;
    lv_obj_t *sub;

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f1b2a), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(0xe6edf3), 0);

    title = lv_label_create(scr);
    lv_label_set_text(title, "Synwit SWM341 + LVGL");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    sub = lv_label_create(scr);
    lv_label_set_text(sub, "800x480 RGB565  |  framebuffers in SDRAM  |  log on RTT");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 80);

    s_lbl_time = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_lbl_time, "00:00");
    lv_obj_align(s_lbl_time, LV_ALIGN_CENTER, 0, -30);

    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, 480, 26);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    s_lbl_info = lv_label_create(scr);
    lv_label_set_text(s_lbl_info, "booting ...");
    lv_obj_align(s_lbl_info, LV_ALIGN_BOTTOM_MID, 0, -30);
}

static void status_timer(lv_timer_t *timer)
{
    char buf[128];
    lv_mem_monitor_t mon;
    uint32_t now = board_millis();
    uint32_t secs = now / 1000;
    uint32_t dt = now - s_period_start;
    uint32_t loops;

    (void)timer;
    if (dt == 0) {
        dt = 1;
    }
    loops = s_loops * 1000 / dt;

    lv_mem_monitor(&mon);

    snprintf(buf, sizeof(buf), "uptime %02u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
    lv_label_set_text(s_lbl_time, buf);

    lv_bar_set_value(s_bar, (secs % 10) * 10, LV_ANIM_OFF);

    snprintf(buf, sizeof(buf), "%u MHz  |  %u loops/s  |  heap %u%% used (%u/%u KB)",
             (unsigned)(SystemCoreClock / 1000000), (unsigned)loops,
             (unsigned)mon.used_pct,
             (unsigned)((mon.total_size - mon.free_size) / 1024),
             (unsigned)(mon.total_size / 1024));
    lv_label_set_text(s_lbl_info, buf);

    SEGGER_RTT_printf(RTT_CH, "%s\n", buf);

    s_loops = 0;
    s_period_start = now;
}

int main(void)
{
    int sdram;

    SystemInit();          /* 120 MHz from the 12 MHz XTAL - see platformio.ini */

    board_sdram_init();    /* framebuffers and LVGL's heap live here - first! */
    board_systick_init();  /* 1 ms tick: board_millis() + lv_tick_inc() */

    SEGGER_RTT_Init();
    SEGGER_RTT_printf(RTT_CH, "SWM341 LVGL demo - SYSCLK %u Hz\n", SystemCoreClock);
    SEGGER_RTT_printf(RTT_CH, "LVGL %u.%u.%u\n",
                      (unsigned)LVGL_VERSION_MAJOR,
                      (unsigned)LVGL_VERSION_MINOR,
                      (unsigned)LVGL_VERSION_PATCH);

    /* bring-up checks: board revision, SDRAM (both framebuffers + the heap).
     * Has to run before lv_init() - it writes into those windows. */
    sdram = board_sdram_test();
    SEGGER_RTT_printf(RTT_CH, "board: %s\n", board_lcd_variant());
    SEGGER_RTT_printf(RTT_CH, "SDRAM: %s\n",
                      sdram == 0 ? "ok (framebuffer 1+2, LVGL heap)" : "FAILED");

    lv_init();
    lv_log_register_print_cb(rtt_log_cb);

    lv_port_disp_init();   /* registers the driver, no hardware access yet */
    ui_create();
    board_lcd_init();      /* pin mux + timing + LCD_Start() */

    /* is the panel really being driven? backlight pin level and frame-done
     * events - 800x480 @62 Hz gives ~12 in 200 ms */
    SEGGER_RTT_printf(RTT_CH, "LCD: BL pin = %d, %u frames in 200 ms\n",
                      board_lcd_bl_level(), (unsigned)board_lcd_frames(200));

    lv_timer_create(status_timer, 1000, NULL);

    s_period_start = board_millis();
    SEGGER_RTT_printf(RTT_CH, "running\n");

    while (1) {
        lv_timer_handler();
        s_loops++;
    }
}

void SysTick_Handler(void)
{
    board_tick();
    lv_tick_inc(1);
}
