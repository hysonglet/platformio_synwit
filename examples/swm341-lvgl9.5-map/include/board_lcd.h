/*
 * Board wiring for the SWM34S demo boards.
 *
 * Everything here is taken from Synwit's own 800x480 ebike demo
 * (02.SWM34S TFT-LCD 驱动例程 / 00.TTL-RGB Demo / 05.SWM34SR&Vxxx_800x480):
 *   Config.h      -> LCD reset / backlight pins, board selection
 *   APP/dev_rgb.c -> RGB565 pin muxing + LCD timing
 *   APP/dev_sdram.c -> SDRAM pin muxing (the framebuffers live there)
 */
#ifndef BOARD_LCD_H
#define BOARD_LCD_H

#include <stdint.h>

#include "SWM341.h"

/*---------------------------------------------------------------------------
 * panel
 *-------------------------------------------------------------------------*/
#define LCD_HDOT          800
#define LCD_VDOT          480
#define LCD_FB_BYTES      (LCD_HDOT * LCD_VDOT * 2)   /* RGB565 -> 768 000 */

/*---------------------------------------------------------------------------
 * SDRAM (8 MB at 0x80000000) layout
 *
 *   +------------------+ 0x80000000  framebuffer 1 (the one LCD_Init starts with)
 *   |  800x480 RGB565  |
 *   +------------------+ 0x800BB800  framebuffer 2
 *   |  800x480 RGB565  |
 *   +------------------+ 0x80177000  LVGL heap (LV_MEM_ADR in lv_conf.h)
 *   |      6 MB        |             LVGL objects + the map tile cache
 *   +------------------+ 0x80777000  (8 MB SDRAM ends at 0x80800000)
 *-------------------------------------------------------------------------*/
#define LCD_FB1_ADR       (SDRAMM_BASE)
#define LCD_FB2_ADR       (LCD_FB1_ADR + LCD_FB_BYTES)
#define LVGL_HEAP_ADR     (LCD_FB2_ADR + LCD_FB_BYTES)   /* 0x80177000 */

/*---------------------------------------------------------------------------
 * pick your board
 *
 *   SWM34SRE_PIN64_A001   SWM34SRET6 LQFP64 - the 5" 800x480 board
 *                         (SWDM-QFP64-34SREB2, panel WKS50089) and the 64-pin
 *                         board of the ebike demo. Backlight PB13, reset PD1.
 *   SWM34SVE_PIN100_A001  SWM34SVET6 LQFP100 (7" panel, backlight boost on PA1)
 *   SWM34SVE_PIN100_A002  SWM34SVET6 LQFP100 (A002/A003)
 *
 * The pin maps are identical for R/G/B and DCLK/VSYNC on all of them; they
 * differ in HSYNC/DEN (PB3+PB4 vs PM8+PM11) and in the backlight pin
 * (PB13 vs PD9). Getting this wrong means a dark panel: the backlight stays
 * off and there is no DE/HSYNC.
 * Override it in platformio.ini, e.g. -DSWM34S_LCM_PCBV=SWM34SVE_PIN100_A002.
 *-------------------------------------------------------------------------*/
#define SWM34SRE_PIN64_A001   1
#define SWM34SVE_PIN100_A001  2
#define SWM34SVE_PIN100_A002  3

#ifndef SWM34S_LCM_PCBV
#define SWM34S_LCM_PCBV       SWM34SRE_PIN64_A001
#endif

#if (SWM34S_LCM_PCBV == SWM34SRE_PIN64_A001)
#define LCD_GPIO_RST  GPIOD
#define LCD_PIN_RST   PIN1
#define LCD_GPIO_BL   GPIOB
#define LCD_PIN_BL    PIN13
#else                                  /* both 100-pin boards */
#define LCD_GPIO_RST  GPIOD
#define LCD_PIN_RST   PIN1
#define LCD_GPIO_BL   GPIOD
#define LCD_PIN_BL    PIN9
#endif

/*---------------------------------------------------------------------------
 * API
 *-------------------------------------------------------------------------*/
void board_sdram_init(void);    /* must run first: the framebuffers live here */
void board_systick_init(void);  /* 1 ms tick, feeds lv_tick_inc() */
void board_lcd_init(void);      /* pin mux + RGB timing + LCD_Start() */

void board_tick(void);          /* called from SysTick_Handler */
uint32_t board_millis(void);
void board_delay_ms(uint32_t ms);

/*---------------------------------------------------------------------------
 * bring-up diagnostics - the first thing to look at when the panel stays dark
 *-------------------------------------------------------------------------*/
const char *board_lcd_variant(void);      /* e.g. "SWM34SRE_PIN64_A001" */
int board_sdram_test(void);               /* 0 = ok, else 1..3 = failing window */
int board_lcd_bl_level(void);             /* level actually read back from the BL pin */
uint32_t board_lcd_frames(uint32_t ms);   /* frame-done events in that window */

#endif /* BOARD_LCD_H */
