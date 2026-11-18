#ifndef LIZARD_H
#define LIZARD_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates a full-screen lv_canvas and a 30 fps LVGL timer that animates the
 * skeleton lizard. Requires lv_init()/lv_port_disp_init() and board_sdram_init()
 * to have run first (the canvas buffer comes out of LVGL's SDRAM heap).
 * Returns the canvas object, or NULL if the buffer could not be allocated. */
lv_obj_t *lizard_create(void);

/* Steer the head by hand (coordinates in LCD pixels). Any touch event
 * overrides this; 2600 ms after the last input the lizard roams on its own. */
void lizard_set_target(uint16_t x, uint16_t y);
void lizard_clear_target(void);

#ifdef __cplusplus
}
#endif

#endif /* LIZARD_H */
