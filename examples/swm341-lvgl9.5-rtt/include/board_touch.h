#ifndef BOARD_TOUCH_H
#define BOARD_TOUCH_H

#include <stdint.h>

/* GT911 (the controller inside the 慧勤 5.0" panel) on I2C1:
 *   SCL = PC5, SDA = PC4, RST = PB11, INT = PD0
 * Same wiring as Synwit's own 5" demo (board/swm34s/swm34sre/a1/dev_tp.c).
 *
 * The chip is polled from LVGL's input device read callback instead of using
 * the INT line as an EXTI source - one I2C transaction every ~30 ms is
 * cheaper than wiring up another interrupt for a demo.
 */
int  board_touch_init(void);                              /* 0 = ok */
void board_touch_read(int16_t *x, int16_t *y, int *pressed);

/* LVGL pointer input device fed by board_touch_read() */
void lv_port_indev_init(void);

#endif /* BOARD_TOUCH_H */
