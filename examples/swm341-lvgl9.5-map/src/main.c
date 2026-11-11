/*
 * SWM341 + 800x480 RGB LCD + LVGL 9.5 + microSD (FatFs), logging over SEGGER RTT.
 *
 *   pio run                build
 *   pio run -t upload      flash with probe-rs and watch the RTT output
 *   JLinkRTTViewer / `probe-rs attach` on a second console also work
 *
 * Nothing is printed on a UART: the log goes through the debug probe, so the
 * only wiring needed is the SWD connector and the LCD ribbon.
 *
 * The card is exposed to LVGL through LV_USE_FS_FATFS: LVGL's own
 * lv_fs_fatfs.c talks to FatFs, so "S:dir/file.txt" is all the UI needs to
 * know (drive letter 'S' -> FatFs volume "sd:", see lv_conf.h).
 */
#include <stdio.h>
#include <string.h>

#include "SWM341.h"
#include "SEGGER_RTT.h"
#include "lvgl.h"

#include "board_lcd.h"
#include "board_sd.h"
#include "lv_port_disp.h"
#include "map_demo.h"

#define RTT_CH 0

static lv_obj_t *s_lbl_time;
static lv_obj_t *s_bar;
static lv_obj_t *s_lbl_info;
static lv_obj_t *s_lbl_sd;

static uint32_t s_loops;
static uint32_t s_period_start;

static int s_sd_res;      /* result of the last board_sd_mount() */
static int s_sd_was_ok;   /* so the log only prints on a change */

/* LV_USE_LOG is on: everything LVGL complains about goes to RTT channel 0 */
static void rtt_log_cb(lv_log_level_t level, const char *buf)
{
    (void)level;
    SEGGER_RTT_WriteString(RTT_CH, buf);
}

static void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
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
    lv_obj_align(s_lbl_info, LV_ALIGN_BOTTOM_MID, 0, -60);

    s_lbl_sd = lv_label_create(scr);
    lv_label_set_text(s_lbl_sd, "SD: ...");
    lv_obj_align(s_lbl_sd, LV_ALIGN_BOTTOM_MID, 0, -30);
}

/* Read a file back through LVGL's file system and list the root directory -
 * this is what proves the 'S:' drive really talks to the card. */
static void sd_lvgl_demo(void)
{
    static const char msg[] = "Hello from SWM341 + LVGL 9.5";
    lv_fs_file_t file;
    char buf[64];
    uint32_t bw = 0, br = 0;
    lv_fs_res_t res;
    int entries = 0;

    if(lv_fs_open(&file, "S:swm341.txt", LV_FS_MODE_WR) != LV_FS_RES_OK) {
        SEGGER_RTT_printf(RTT_CH, "SD: lv_fs_open(WR) failed\n");
        return;
    }
    lv_fs_write(&file, msg, strlen(msg), &bw);
    lv_fs_close(&file);

    res = lv_fs_open(&file, "S:swm341.txt", LV_FS_MODE_RD);
    if(res != LV_FS_RES_OK) {
        SEGGER_RTT_printf(RTT_CH, "SD: lv_fs_open(RD) failed\n");
        return;
    }
    lv_fs_read(&file, buf, sizeof(buf) - 1, &br);
    buf[br < sizeof(buf) - 1 ? br : sizeof(buf) - 1] = '\0';
    lv_fs_close(&file);

    SEGGER_RTT_printf(RTT_CH, "SD: wrote %u B, read back \"%s\" (%s)\n",
                      (unsigned)bw, buf,
                      strcmp(buf, msg) == 0 ? "match" : "MISMATCH");

    lv_fs_dir_t dir;
    if(lv_fs_dir_open(&dir, "S:/") == LV_FS_RES_OK) {
        char name[64];

        while(lv_fs_dir_read(&dir, name, sizeof(name)) == LV_FS_RES_OK && name[0] != '\0') {
            SEGGER_RTT_printf(RTT_CH, "SD: root entry: %s\n", name);
            if(++entries >= 10) {
                break;
            }
        }
        lv_fs_dir_close(&dir);
    }
    SEGGER_RTT_printf(RTT_CH, "SD: %d entries\n", entries);
}

/* Keeps the SD line on screen up to date and re-mounts when a card is put in
 * later (there is no card-detect pin, so this is the only way to notice). */
static void sd_refresh(void)
{
    uint32_t total_kb = 0, free_kb = 0;
    char buf[64];
    int ok;

    if(!board_sd_mounted()) {
        s_sd_res = board_sd_mount();
    }
    ok = board_sd_mounted() && board_sd_info(&total_kb, &free_kb) == 0;

    if(ok) {
        snprintf(buf, sizeof(buf), "SD: %s  |  %u MB free of %u MB",
                 board_sd_filesystem(),
                 (unsigned)(free_kb / 1024), (unsigned)(total_kb / 1024));
    }
    else {
        snprintf(buf, sizeof(buf), "SD: no card (%d)", s_sd_res);
    }
    lv_label_set_text(s_lbl_sd, buf);

    if(ok != s_sd_was_ok) {
        s_sd_was_ok = ok;
        if(ok) {
            SEGGER_RTT_printf(RTT_CH, "SD: %s\n", buf);
            sd_lvgl_demo();
        }
        else {
            SEGGER_RTT_printf(RTT_CH, "SD: mount failed (%d) - no card?\n", s_sd_res);
        }
    }
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
    if(dt == 0) {
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

    sd_refresh();

    // SEGGER_RTT_printf(RTT_CH, "%s\n", buf);

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

    lv_port_disp_init();   /* creates the display, no hardware access yet */
    ui_create();
    board_lcd_init();      /* pin mux + timing + LCD_Start() */

    /* is the panel really being driven? backlight pin level and frame-done
     * events - 800x480 @62 Hz gives ~12 in 200 ms */
    SEGGER_RTT_printf(RTT_CH, "LCD: BL pin = %d, %u frames in 200 ms\n",
                      board_lcd_bl_level(), (unsigned)board_lcd_frames(200));

    /* SD card: FatFs over SDIO. LVGL's fs driver is registered by lv_init()
     * (LV_USE_FS_FATFS), so "S:..." works as soon as the volume is mounted. */
    s_sd_res = board_sd_mount();
    SEGGER_RTT_printf(RTT_CH, "SD: mount result = %d\n", s_sd_res);
    sd_refresh();

    /* Offline map preview: every *.ltb in sd:/maps, drawn by libs/map_view.
     * It takes over the screen - the status screen keeps running, its labels
     * are just not visible any more. */
    map_demo_init();
    map_demo_create();

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
