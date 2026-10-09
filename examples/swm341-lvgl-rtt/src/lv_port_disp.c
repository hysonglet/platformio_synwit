/*
 * LVGL display driver for the SWM341 LCD controller (RGB / TTL interface).
 *
 * This is the "direct framebuffer" setup of the vendor demo (lv_port_disp3.c):
 * the two screen-sized buffers in SDRAM *are* the framebuffers, LVGL renders
 * into one while the LCD controller scans the other out, and flushing only
 * re-points layer 0 at the buffer that was just rendered.
 */
#include "lvgl.h"

#include "board_lcd.h"
#include "lv_port_disp.h"

static lv_disp_drv_t s_disp_drv;
static lv_disp_draw_buf_t s_draw_buf;

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    (void)area;   /* the whole screen is always rendered (full_refresh = 1) */

    LCD->L[0].ADDR = (uint32_t)color_p;
    LCD->CR |= (1 << LCD_CR_VBPRELOAD_Pos);        /* take effect at next VBP */
    while (LCD->CR & LCD_CR_VBPRELOAD_Msk) {
        __NOP();
    }

    lv_disp_flush_ready(drv);
}

void lv_port_disp_init(void)
{
    lv_disp_draw_buf_init(&s_draw_buf,
                          (void *)LCD_FB1_ADR,
                          (void *)LCD_FB2_ADR,
                          LCD_HDOT * LCD_VDOT);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_HDOT;
    s_disp_drv.ver_res = LCD_VDOT;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.flush_cb = disp_flush;
    s_disp_drv.full_refresh = 1;   /* screen-sized buffers: redraw everything */
    lv_disp_drv_register(&s_disp_drv);
}
