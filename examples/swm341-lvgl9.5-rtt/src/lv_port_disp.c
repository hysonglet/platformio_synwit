/*
 * LVGL 9.5 display driver for the SWM341 LCD controller (RGB / TTL interface).
 *
 * This is the "direct framebuffer" setup of the vendor demo (lv_port_disp3.c):
 * the two screen-sized buffers in SDRAM *are* the framebuffers, LVGL renders
 * into one while the LCD controller scans the other out, and flushing only
 * re-points layer 0 at the buffer that was just rendered.
 */
#include "lvgl.h"

#include "board_lcd.h"
#include "lv_port_disp.h"

static lv_display_t *s_disp;

static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;   /* the whole screen is always rendered (RENDER_MODE_FULL) */

    LCD->L[0].ADDR = (uint32_t)px_map;
    LCD->CR |= (1 << LCD_CR_VBPRELOAD_Pos);        /* take effect at next VBP */
    while (LCD->CR & LCD_CR_VBPRELOAD_Msk) {
        __NOP();
    }

    lv_display_flush_ready(disp);
}

lv_display_t *lv_port_disp_init(void)
{
    /* 800x480 RGB565: LV_COLOR_FORMAT_RGB565 keeps LVGL from assuming the
     * 32-bit layout LV_COLOR_FORMAT_NATIVE would give it on some setups. */
    s_disp = lv_display_create(LCD_HDOT, LCD_VDOT);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    /* buf_size is in bytes; RENDER_MODE_FULL always redraws the whole screen,
     * which is what two screen-sized framebuffers can afford. */
    lv_display_set_buffers(s_disp,
                           (void *)LCD_FB1_ADR,
                           (void *)LCD_FB2_ADR,
                           LCD_HDOT * LCD_VDOT * (LV_COLOR_DEPTH / 8),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(s_disp, disp_flush);

    /* lv_display_create() already applies the default theme; make sure
     * lv_screen_active() and friends use this display. */
    lv_display_set_default(s_disp);

    return s_disp;
}
