#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Registers an LVGL display driver on top of the SWM341 LCD controller.
 * board_sdram_init()/board_lcd_init() have to run before/around it - see
 * main.c for the ordering. */
void lv_port_disp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_DISP_H */
