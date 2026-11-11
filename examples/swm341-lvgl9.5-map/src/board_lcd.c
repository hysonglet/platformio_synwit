/*
 * SWM34S demo board bring-up: SDRAM -> SysTick -> RGB LCD.
 *
 * Ported from Synwit's 800x480 ebike demo (APP/dev_sdram.c, APP/dev_rgb.c,
 * APP/dev_systick.c) and reduced to what a plain LVGL display needs:
 * no touch, no JPEG, no SPI flash.
 */
#include <string.h>

#include "board_lcd.h"

/*---------------------------------------------------------------------------
 * 1 ms time base
 *-------------------------------------------------------------------------*/
static volatile uint32_t s_ticks;

void board_tick(void)
{
    s_ticks++;
}

uint32_t board_millis(void)
{
    return s_ticks;
}

void board_delay_ms(uint32_t ms)
{
    uint32_t start = s_ticks;

    while (s_ticks - start < ms) {
        __NOP();
    }
}

void board_systick_init(void)
{
    /* 1 ms, same as the vendor demo */
    SysTick_Config(SystemCoreClock / 1000);
}

/*---------------------------------------------------------------------------
 * SDRAM - 8 MB, holds both framebuffers and LVGL's heap
 *-------------------------------------------------------------------------*/
void board_sdram_init(void)
{
    SDRAM_InitStructure SDRAM_InitStruct;

    PORT_Init(PORTM, PIN13, PORTM_PIN13_SDR_CLK, 0);
    PORT_Init(PORTM, PIN14, PORTM_PIN14_SDR_CKE, 0);
    PORT_Init(PORTB, PIN7, PORTB_PIN7_SDR_WE, 0);
    PORT_Init(PORTB, PIN8, PORTB_PIN8_SDR_CAS, 0);
    PORT_Init(PORTB, PIN9, PORTB_PIN9_SDR_RAS, 0);
    PORT_Init(PORTB, PIN10, PORTB_PIN10_SDR_CS, 0);
    PORT_Init(PORTE, PIN15, PORTE_PIN15_SDR_BA0, 0);
    PORT_Init(PORTE, PIN14, PORTE_PIN14_SDR_BA1, 0);
    PORT_Init(PORTN, PIN14, PORTN_PIN14_SDR_A0, 0);
    PORT_Init(PORTN, PIN13, PORTN_PIN13_SDR_A1, 0);
    PORT_Init(PORTN, PIN12, PORTN_PIN12_SDR_A2, 0);
    PORT_Init(PORTN, PIN11, PORTN_PIN11_SDR_A3, 0);
    PORT_Init(PORTN, PIN10, PORTN_PIN10_SDR_A4, 0);
    PORT_Init(PORTN, PIN9, PORTN_PIN9_SDR_A5, 0);
    PORT_Init(PORTN, PIN8, PORTN_PIN8_SDR_A6, 0);
    PORT_Init(PORTN, PIN7, PORTN_PIN7_SDR_A7, 0);
    PORT_Init(PORTN, PIN6, PORTN_PIN6_SDR_A8, 0);
    PORT_Init(PORTN, PIN3, PORTN_PIN3_SDR_A9, 0);
    PORT_Init(PORTN, PIN15, PORTN_PIN15_SDR_A10, 0);
    PORT_Init(PORTN, PIN2, PORTN_PIN2_SDR_A11, 0);
    PORT_Init(PORTM, PIN15, PORTM_PIN15_SDR_A12, 0);

    /* data bus - input enable set, as in the vendor demo */
    PORT_Init(PORTE, PIN7, PORTE_PIN7_SDR_D0, 1);
    PORT_Init(PORTE, PIN6, PORTE_PIN6_SDR_D1, 1);
    PORT_Init(PORTE, PIN5, PORTE_PIN5_SDR_D2, 1);
    PORT_Init(PORTE, PIN4, PORTE_PIN4_SDR_D3, 1);
    PORT_Init(PORTE, PIN3, PORTE_PIN3_SDR_D4, 1);
    PORT_Init(PORTE, PIN2, PORTE_PIN2_SDR_D5, 1);
    PORT_Init(PORTE, PIN1, PORTE_PIN1_SDR_D6, 1);
    PORT_Init(PORTE, PIN0, PORTE_PIN0_SDR_D7, 1);
    PORT_Init(PORTE, PIN8, PORTE_PIN8_SDR_D8, 1);
    PORT_Init(PORTE, PIN9, PORTE_PIN9_SDR_D9, 1);
    PORT_Init(PORTE, PIN10, PORTE_PIN10_SDR_D10, 1);
    PORT_Init(PORTE, PIN11, PORTE_PIN11_SDR_D11, 1);
    PORT_Init(PORTE, PIN12, PORTE_PIN12_SDR_D12, 1);
    PORT_Init(PORTE, PIN13, PORTE_PIN13_SDR_D13, 1);
    PORT_Init(PORTC, PIN14, PORTC_PIN14_SDR_D14, 1);
    PORT_Init(PORTC, PIN15, PORTC_PIN15_SDR_D15, 1);

    PORT_Init(PORTB, PIN6, PORTB_PIN6_SDR_LDQM, 0);
    PORT_Init(PORTM, PIN12, PORTM_PIN12_SDR_UDQM, 0);

    SDRAM_InitStruct.Size = SDRAM_SIZE_8MB;
    SDRAM_InitStruct.ClkDiv = SDRAM_CLKDIV_1;      /* CPU 20-140 MHz */
    SDRAM_InitStruct.CASLatency = SDRAM_CASLATENCY_3;
    /* This CSL is newer than the one the ebike demo ships with: it renamed
     * TimeTRFC/SDRAM_TRFC_9 to TimeTRC/SDRAM_TRC_9 and added RefreshTime.
     * RefreshTime is the window in which all rows must be refreshed (64 ms is
     * the usual SDRAM figure) - leave it at 0 and the controller refreshes
     * far more often than necessary. */
    SDRAM_InitStruct.RefreshTime = 64;
    SDRAM_InitStruct.TimeTRP = SDRAM_TRP_2;
    SDRAM_InitStruct.TimeTRCD = SDRAM_TRCD_2;
    SDRAM_InitStruct.TimeTRC = SDRAM_TRC_9;
    SDRAM_Init(&SDRAM_InitStruct);
}

/*---------------------------------------------------------------------------
 * RGB565 pin muxing, one function per demo board revision
 *-------------------------------------------------------------------------*/
#if (SWM34S_LCM_PCBV == SWM34SRE_PIN64_A001)

static void rgb_pins_init(void)
{
    /* R[3:7] */
    PORT_Init(PORTC, PIN9, PORTC_PIN9_LCD_R3, 0);
    PORT_Init(PORTC, PIN10, PORTC_PIN10_LCD_R4, 0);
    PORT_Init(PORTC, PIN11, PORTC_PIN11_LCD_R5, 0);
    PORT_Init(PORTC, PIN12, PORTC_PIN12_LCD_R6, 0);
    PORT_Init(PORTC, PIN13, PORTC_PIN13_LCD_R7, 0);
    /* G[2:7] */
    PORT_Init(PORTA, PIN14, PORTA_PIN14_LCD_G2, 0);
    PORT_Init(PORTA, PIN15, PORTA_PIN15_LCD_G3, 0);
    PORT_Init(PORTC, PIN0, PORTC_PIN0_LCD_G4, 0);
    PORT_Init(PORTC, PIN1, PORTC_PIN1_LCD_G5, 0);
    PORT_Init(PORTC, PIN2, PORTC_PIN2_LCD_G6, 0);
    PORT_Init(PORTC, PIN3, PORTC_PIN3_LCD_G7, 0);
    /* B[3:7] */
    PORT_Init(PORTB, PIN15, PORTB_PIN15_LCD_B3, 0);
    PORT_Init(PORTA, PIN2, PORTA_PIN2_LCD_B4, 0);
    PORT_Init(PORTA, PIN9, PORTA_PIN9_LCD_B5, 0);
    PORT_Init(PORTA, PIN10, PORTA_PIN10_LCD_B6, 0);
    PORT_Init(PORTA, PIN11, PORTA_PIN11_LCD_B7, 0);

    /* DCLK / VSYNC / HSYNC / DE */
    PORT_Init(PORTB, PIN5, PORTB_PIN5_LCD_DCLK, 0);
    PORT_Init(PORTB, PIN2, PORTB_PIN2_LCD_VSYNC, 0);
    PORT_Init(PORTB, PIN3, PORTB_PIN3_LCD_HSYNC, 0);
    PORT_Init(PORTB, PIN4, PORTB_PIN4_LCD_DEN, 0);
}

#elif (SWM34S_LCM_PCBV == SWM34SVE_PIN100_A001)

static void rgb_pins_init(void)
{
    /* backlight boost (AP3012KTR-E1) of the 7" board */
    GPIO_Init(GPIOA, PIN1, 1, 1, 0, 0);
    GPIO_SetBit(GPIOA, PIN1);

    /* the pins below carry no RGB signal on this board - driven low */
    GPIO_Init(GPIOC, PIN4, 1, 0, 0, 0);
    GPIO_Init(GPIOC, PIN5, 1, 0, 0, 0);
    GPIO_Init(GPIOC, PIN8, 1, 0, 0, 0);
    GPIO_ClrBit(GPIOC, PIN4);
    GPIO_ClrBit(GPIOC, PIN5);
    GPIO_ClrBit(GPIOC, PIN8);

    GPIO_Init(GPIOA, PIN12, 1, 0, 0, 0);
    GPIO_Init(GPIOA, PIN13, 1, 0, 0, 0);
    GPIO_ClrBit(GPIOA, PIN12);
    GPIO_ClrBit(GPIOA, PIN13);

    GPIO_Init(GPIOB, PIN1, 1, 0, 0, 0);
    GPIO_Init(GPIOB, PIN11, 1, 0, 0, 0);
    GPIO_Init(GPIOB, PIN13, 1, 0, 0, 0);
    GPIO_ClrBit(GPIOB, PIN1);
    GPIO_ClrBit(GPIOB, PIN11);
    GPIO_ClrBit(GPIOB, PIN13);

    /* R[3:7] */
    PORT_Init(PORTC, PIN9, PORTC_PIN9_LCD_R3, 0);
    PORT_Init(PORTC, PIN10, PORTC_PIN10_LCD_R4, 0);
    PORT_Init(PORTC, PIN11, PORTC_PIN11_LCD_R5, 0);
    PORT_Init(PORTC, PIN12, PORTC_PIN12_LCD_R6, 0);
    PORT_Init(PORTC, PIN13, PORTC_PIN13_LCD_R7, 0);
    /* G[2:7] */
    PORT_Init(PORTA, PIN14, PORTA_PIN14_LCD_G2, 0);
    PORT_Init(PORTA, PIN15, PORTA_PIN15_LCD_G3, 0);
    PORT_Init(PORTC, PIN0, PORTC_PIN0_LCD_G4, 0);
    PORT_Init(PORTC, PIN1, PORTC_PIN1_LCD_G5, 0);
    PORT_Init(PORTC, PIN2, PORTC_PIN2_LCD_G6, 0);
    PORT_Init(PORTC, PIN3, PORTC_PIN3_LCD_G7, 0);
    /* B[3:7] */
    PORT_Init(PORTB, PIN15, PORTB_PIN15_LCD_B3, 0);
    PORT_Init(PORTA, PIN2, PORTA_PIN2_LCD_B4, 0);
    PORT_Init(PORTA, PIN9, PORTA_PIN9_LCD_B5, 0);
    PORT_Init(PORTA, PIN10, PORTA_PIN10_LCD_B6, 0);
    PORT_Init(PORTA, PIN11, PORTA_PIN11_LCD_B7, 0);

    /* DCLK / VSYNC / HSYNC / DE */
    PORT_Init(PORTB, PIN5, PORTB_PIN5_LCD_DCLK, 0);
    PORT_Init(PORTB, PIN2, PORTB_PIN2_LCD_VSYNC, 0);
    PORT_Init(PORTB, PIN3, PORTB_PIN3_LCD_HSYNC, 0);
    PORT_Init(PORTB, PIN4, PORTB_PIN4_LCD_DEN, 0);
}

#elif (SWM34S_LCM_PCBV == SWM34SVE_PIN100_A002)

static void rgb_pins_init(void)
{
    /* the pins below carry no RGB signal on this board - driven low */
    GPIO_Init(GPIOC, PIN4, 1, 0, 0, 0);
    GPIO_Init(GPIOC, PIN5, 1, 0, 0, 0);
    GPIO_Init(GPIOC, PIN8, 1, 0, 0, 0);
    GPIO_ClrBit(GPIOC, PIN4);
    GPIO_ClrBit(GPIOC, PIN5);
    GPIO_ClrBit(GPIOC, PIN8);

    GPIO_Init(GPIOA, PIN12, 1, 0, 0, 0);
    GPIO_Init(GPIOA, PIN13, 1, 0, 0, 0);
    GPIO_ClrBit(GPIOA, PIN12);
    GPIO_ClrBit(GPIOA, PIN13);

    GPIO_Init(GPIOB, PIN1, 1, 0, 0, 0);
    GPIO_Init(GPIOB, PIN11, 1, 0, 0, 0);
    GPIO_Init(GPIOB, PIN13, 1, 0, 0, 0);
    GPIO_ClrBit(GPIOB, PIN1);
    GPIO_ClrBit(GPIOB, PIN11);
    GPIO_ClrBit(GPIOB, PIN13);

    /* R[3:7] */
    PORT_Init(PORTC, PIN9, PORTC_PIN9_LCD_R3, 0);
    PORT_Init(PORTC, PIN10, PORTC_PIN10_LCD_R4, 0);
    PORT_Init(PORTC, PIN11, PORTC_PIN11_LCD_R5, 0);
    PORT_Init(PORTC, PIN12, PORTC_PIN12_LCD_R6, 0);
    PORT_Init(PORTC, PIN13, PORTC_PIN13_LCD_R7, 0);
    /* G[2:7] */
    PORT_Init(PORTA, PIN14, PORTA_PIN14_LCD_G2, 0);
    PORT_Init(PORTA, PIN15, PORTA_PIN15_LCD_G3, 0);
    PORT_Init(PORTC, PIN0, PORTC_PIN0_LCD_G4, 0);
    PORT_Init(PORTC, PIN1, PORTC_PIN1_LCD_G5, 0);
    PORT_Init(PORTC, PIN2, PORTC_PIN2_LCD_G6, 0);
    PORT_Init(PORTC, PIN3, PORTC_PIN3_LCD_G7, 0);
    /* B[3:7] */
    PORT_Init(PORTB, PIN15, PORTB_PIN15_LCD_B3, 0);
    PORT_Init(PORTA, PIN2, PORTA_PIN2_LCD_B4, 0);
    PORT_Init(PORTA, PIN9, PORTA_PIN9_LCD_B5, 0);
    PORT_Init(PORTA, PIN10, PORTA_PIN10_LCD_B6, 0);
    PORT_Init(PORTA, PIN11, PORTA_PIN11_LCD_B7, 0);

    /* DCLK / VSYNC / HSYNC / DE - A002 routes HSYNC/DE to PORTM */
    PORT_Init(PORTB, PIN5, PORTB_PIN5_LCD_DCLK, 0);
    PORT_Init(PORTB, PIN2, PORTB_PIN2_LCD_VSYNC, 0);
    PORT_Init(PORTM, PIN8, PORTM_PIN8_LCD_HSYNC, 0);
    PORT_Init(PORTM, PIN11, PORTM_PIN11_LCD_DEN, 0);
}

#else
#error "Unknown SWM34S_LCM_PCBV - see include/board_lcd.h"
#endif

/*---------------------------------------------------------------------------
 * LCD controller
 *-------------------------------------------------------------------------*/
void board_lcd_init(void)
{
    LCD_InitStructure LCD_initStruct;

    rgb_pins_init();

    /* reset pulse */
    GPIO_Init(LCD_GPIO_RST, LCD_PIN_RST, 1, 0, 0, 0);
    GPIO_ClrBit(LCD_GPIO_RST, LCD_PIN_RST);
    board_delay_ms(1);
    GPIO_SetBit(LCD_GPIO_RST, LCD_PIN_RST);

    /* backlight on */
    GPIO_Init(LCD_GPIO_BL, LCD_PIN_BL, 1, 0, 0, 0);
    GPIO_SetBit(LCD_GPIO_BL, LCD_PIN_BL);

    /* 800x480 timing of the vendor demo.
     * ClkDiv 4 -> DOTCLK = SYSCLK / 4 = 120 MHz / 4 = 30 MHz
     *            (the demo runs 150 MHz with ClkDiv 5 - same 30 MHz).
     *            915 * 530 * 30 MHz -> ~62 Hz. */
    LCD_initStruct.ClkDiv = 4;
    LCD_initStruct.Format = LCD_FMT_RGB565;
    LCD_initStruct.HnPixel = LCD_HDOT;
    LCD_initStruct.VnPixel = LCD_VDOT;
    LCD_initStruct.Hfp = 64;
    LCD_initStruct.Hbp = 46;
    LCD_initStruct.Vfp = 22;
    LCD_initStruct.Vbp = 23;
    LCD_initStruct.HsyncWidth = 5;
    LCD_initStruct.VsyncWidth = 5;
    LCD_initStruct.DataSource = LCD_FB1_ADR;
    LCD_initStruct.Background = 0xFFFFFF;
    LCD_initStruct.SampleEdge = LCD_SAMPLE_FALL;
    LCD_initStruct.IntEOTEn = 0;
    LCD_Init(LCD, &LCD_initStruct);

    /* black start-up screen: 0x0000 is black in RGB565 */
    memset((void *)LCD_FB1_ADR, 0, LCD_FB_BYTES);
    memset((void *)LCD_FB2_ADR, 0, LCD_FB_BYTES);

    LCD_Start(LCD);
}

/*---------------------------------------------------------------------------
 * bring-up diagnostics
 *-------------------------------------------------------------------------*/
const char *board_lcd_variant(void)
{
#if (SWM34S_LCM_PCBV == SWM34SRE_PIN64_A001)
    return "SWM34SRE_PIN64_A001 (SWM34SRET6 LQFP64)";
#elif (SWM34S_LCM_PCBV == SWM34SVE_PIN100_A001)
    return "SWM34SVE_PIN100_A001 (SWM34SVET6 LQFP100)";
#else
    return "SWM34SVE_PIN100_A002 (SWM34SVET6 LQFP100)";
#endif
}

/* Write a pattern to the three SDRAM windows the example uses and read it back.
 * Run it *before* lv_init()/board_lcd_init(): it scribbles over both
 * framebuffers and over LVGL's heap.
 * Returns 0 when everything stuck, else the number of the failing window. */
int board_sdram_test(void)
{
    static const uint32_t pattern[4] = { 0xA5A5A5A5, 0x5A5A5A5A, 0x12345678, 0xDEADBEEF };
    const uint32_t window[3] = { LCD_FB1_ADR, LCD_FB2_ADR, LVGL_HEAP_ADR };
    uint32_t w, i;

    for (w = 0; w < 3; w++) {
        volatile uint32_t *p = (volatile uint32_t *)window[w];

        for (i = 0; i < 256; i++) {
            p[i] = pattern[i & 3];
        }
        for (i = 0; i < 256; i++) {
            if (p[i] != pattern[i & 3]) {
                return (int)w + 1;
            }
        }
    }

    return 0;
}

/* GPIO_Init() enables the input buffer, so IDR shows what is really on the pin. */
int board_lcd_bl_level(void)
{
    return (int)((LCD_GPIO_BL->IDR >> LCD_PIN_BL) & 1);
}

/* Rough cross-check that the LCD controller is actually scanning out frames:
 * clear the frame-done flag and count how often it comes back. A running
 * 800x480 @62 Hz setup gives ~12 events in 200 ms, a stalled one gives 0. */
uint32_t board_lcd_frames(uint32_t ms)
{
    uint32_t frames = 0;
    uint32_t start = board_millis();

    LCD->IF = 1;
    while ((board_millis() - start) < ms) {
        if (LCD->IF & 1) {
            LCD->IF = 1;
            frames++;
        }
    }

    return frames;
}
