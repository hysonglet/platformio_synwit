#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates an LVGL 9 display on top of the SWM341 LCD controller and makes it
 * the default one. board_sdram_init()/board_lcd_init() have to run before/
 * around it - see main.c for the ordering. */
lv_display_t *lv_port_disp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_DISP_H */
