/*
 * GT911 touch controller -> LVGL pointer input device.
 *
 * Ported from Synwit's 5" demo (peripheral/tp/tp_gt9x.c) and stripped down:
 * no chip auto-detection, no config upload (the panel ships with its own),
 * and polling instead of an EXTI interrupt.
 *
 * Which I2C the panel sits on depends on the board revision, and the vendor
 * demo's answer (I2C1 PC5/PC4) did not work here, so init walks every I2C0 /
 * I2C1 pin pair that is free on this board and scans the whole address range,
 * printing what answers over RTT. Once the pair is known it can be hardcoded
 * and the scan thrown away.
 */
#include "board_touch.h"

#include "SWM341.h"
#include "SEGGER_RTT.h"
#include "lvgl.h"

#include "board_lcd.h"

#define TP_I2C_CLK    400000

#define GTP_CTRL      0x8040
#define GTP_PID       0x8140
#define GTP_STATUS    0x814E
#define GTP_TP1       0x8150

/* Panel orientation: if the pointer moves the wrong way, flip these. */
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY   0
#endif
#ifndef TOUCH_MIRROR_X
#define TOUCH_MIRROR_X  0
#endif
#ifndef TOUCH_MIRROR_Y
#define TOUCH_MIRROR_Y  1     /* Synwit's demo mirrors Y as well */
#endif

typedef struct {
    I2C_TypeDef  *i2c;
    PORT_TypeDef *scl_port;
    uint8_t       scl_pin;
    uint32_t      scl_func;
    PORT_TypeDef *sda_port;
    uint8_t       sda_pin;
    uint32_t      sda_func;
    const char   *name;
} tp_bus_t;

/* every I2C pin pair that is NOT used by the LCD / SDRAM / SDIO on this board
 * (skipped: PC1+PC2 = LCD_G5/G6, PC13 = LCD_R7, PB13 = backlight,
 * PB15 = LCD_B3, PM5/PM6 + PN0/PN1 = SD card) */
static const tp_bus_t s_buses[] = {
    { I2C1, PORTC, PIN5, PORTC_PIN5_I2C1_SCL, PORTC, PIN4, PORTC_PIN4_I2C1_SDA, "I2C1 PC5/PC4" },
    { I2C1, PORTA, PIN7, PORTA_PIN7_I2C1_SCL, PORTA, PIN6, PORTA_PIN6_I2C1_SDA, "I2C1 PA7/PA6" },
    { I2C1, PORTD, PIN8, PORTD_PIN8_I2C1_SCL, PORTD, PIN7, PORTD_PIN7_I2C1_SDA, "I2C1 PD8/PD7" },
    { I2C0, PORTA, PIN1, PORTA_PIN1_I2C0_SCL, PORTA, PIN0, PORTA_PIN0_I2C0_SDA, "I2C0 PA1/PA0" },
    { I2C0, PORTA, PIN4, PORTA_PIN4_I2C0_SCL, PORTA, PIN3, PORTA_PIN3_I2C0_SDA, "I2C0 PA4/PA3" },
    { I2C0, PORTN, PIN5, PORTN_PIN5_I2C0_SCL, PORTN, PIN4, PORTN_PIN4_I2C0_SDA, "I2C0 PN5/PN4" },
};

static int      s_ready;
static I2C_TypeDef *s_i2c = I2C1;
static uint8_t  s_addr = 0x5D;
static int16_t  s_last_x;
static int16_t  s_last_y;

/*---------------------------------------------------------------------------
 * I2C
 *-------------------------------------------------------------------------*/
static int tp_start(uint8_t rw)
{
    return I2C_Start(s_i2c, (uint8_t)((s_addr << 1) | rw), 1) ? 1 : 0;
}

static void tp_stop(void)
{
    I2C_Stop(s_i2c, 1);
}

static int tp_write(uint16_t reg, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if(!tp_start(0)) goto fail;
    if(!I2C_Write(s_i2c, (uint8_t)(reg >> 8), 1)) goto fail;
    if(!I2C_Write(s_i2c, (uint8_t)(reg & 0xFF), 1)) goto fail;
    for(i = 0; i < len; i++) {
        if(!I2C_Write(s_i2c, buf[i], 1)) goto fail;
    }
    tp_stop();
    return 1;

fail:
    tp_stop();
    return 0;
}

static int tp_read(uint16_t reg, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if(!tp_start(0)) goto fail;
    if(!I2C_Write(s_i2c, (uint8_t)(reg >> 8), 1)) goto fail;
    if(!I2C_Write(s_i2c, (uint8_t)(reg & 0xFF), 1)) goto fail;
    if(!tp_start(1)) goto fail;                 /* repeated start, read */
    for(i = 0; i + 1 < len; i++) {
        buf[i] = I2C_Read(s_i2c, 1, 1);
    }
    buf[i] = I2C_Read(s_i2c, 0, 1);
    tp_stop();
    return 1;

fail:
    tp_stop();
    return 0;
}

/*---------------------------------------------------------------------------
 * bring-up: wire the bus, then look for anything that answers
 *-------------------------------------------------------------------------*/
static void bus_init(const tp_bus_t *b)
{
    I2C_InitStructure i2c;

    /* The CSL needs OPEND + PULLU on both lines - without them the bus never
     * sees an ACK. */
    PORT_Init(b->scl_port, b->scl_pin, b->scl_func, 1);
    b->scl_port->OPEND |= (1 << b->scl_pin);
    b->scl_port->PULLU |= (1 << b->scl_pin);

    PORT_Init(b->sda_port, b->sda_pin, b->sda_func, 1);
    b->sda_port->OPEND |= (1 << b->sda_pin);
    b->sda_port->PULLU |= (1 << b->sda_pin);

    i2c.Master        = 1;
    i2c.MstClk        = TP_I2C_CLK;
    i2c.Addr10b       = 0;
    i2c.TXEmptyIEn    = 0;
    i2c.RXNotEmptyIEn = 0;
    I2C_Init(b->i2c, &i2c);
    I2C_Open(b->i2c);

    s_i2c = b->i2c;
}

/* RST = PB11, INT = PD0. The level on INT while RST rises picks the address. */
static void tp_reset(uint8_t addr)
{
    GPIO_Init(GPIOB, PIN11, 1, 1, 0, 0);        /* RST: output */
    GPIO_Init(GPIOD, PIN0, 1, 0, 0, 0);         /* INT: push-pull output */

    if(addr == 0x5D) GPIO_ClrBit(GPIOD, PIN0);
    else             GPIO_SetBit(GPIOD, PIN0);

    GPIO_ClrBit(GPIOB, PIN11);
    board_delay_ms(10);
    GPIO_SetBit(GPIOB, PIN11);
    board_delay_ms(10);

    GPIO_Init(GPIOD, PIN0, 0, 0, 1, 0);         /* INT back to input + pull-down */
    board_delay_ms(50);                          /* the chip needs a moment */
}

static int try_addr(uint8_t addr7)
{
    if(I2C_Start(s_i2c, (uint8_t)(addr7 << 1), 1)) {
        I2C_Stop(s_i2c, 1);
        return 1;
    }
    I2C_Stop(s_i2c, 1);
    return 0;
}

static void print_scan(const char *bus, uint8_t addr)
{
    SEGGER_RTT_printf(0, "tp: scan %s (%s): ", bus, addr == 0x5D ? "INT low" : "INT high");
    for(uint8_t a = 0x08; a < 0x78; a++) {
        if(try_addr(a)) SEGGER_RTT_printf(0, "0x%02X ", a);
    }
    SEGGER_RTT_printf(0, "\n");
}

int board_touch_init(void)
{
    static const uint8_t addrs[2] = { 0x5D, 0x14 };
    int b, a;

    s_ready = 0;

    for(b = 0; b < (int)(sizeof(s_buses) / sizeof(s_buses[0])); b++) {
        bus_init(&s_buses[b]);

        for(a = 0; a < 2; a++) {
            uint8_t pid[4];

            tp_reset(addrs[a]);
            if(!try_addr(addrs[a])) continue;    /* nobody home at that address */

            s_addr = addrs[a];
            if(tp_read(GTP_PID, pid, sizeof(pid))) {
                pid[3] = '\0';
                SEGGER_RTT_printf(0, "tp: GT9xx on %s, addr 0x%02X, id \"%s\"\n",
                                  s_buses[b].name, addrs[a], (char *)pid);
                s_ready = 1;
                return 0;
            }
        }

        /* nothing usable: dump the whole bus so the wiring can be checked */
        tp_reset(0x5D);
        print_scan(s_buses[b].name, 0x5D);
    }

    SEGGER_RTT_printf(0, "tp: no GT9xx found on any I2C pin pair\n");
    return -1;
}

/*---------------------------------------------------------------------------
 * read
 *-------------------------------------------------------------------------*/
void board_touch_read(int16_t *x, int16_t *y, int *pressed)
{
    uint8_t status;
    uint8_t zero = 0;
    uint8_t num;

    *x = s_last_x;
    *y = s_last_y;
    *pressed = 0;

    if(!s_ready) return;

    if(!tp_read(GTP_STATUS, &status, 1)) return;

    num = status & 0x0F;
    if(status & 0x80) {
        tp_write(GTP_STATUS, &zero, 1);      /* clear the READY flag */
    }

    if(num == 0 || num >= 6) return;         /* released, or garbage */

    uint8_t buf[4];
    if(!tp_read(GTP_TP1, buf, 4)) return;

    int16_t tx = (int16_t)(((uint16_t)buf[1] << 8) | buf[0]);
    int16_t ty = (int16_t)(((uint16_t)buf[3] << 8) | buf[2]);

#if TOUCH_SWAP_XY
    int16_t t = tx; tx = ty; ty = t;
#endif
#if TOUCH_MIRROR_X
    tx = (int16_t)(LCD_HDOT - 1 - tx);
#endif
#if TOUCH_MIRROR_Y
    ty = (int16_t)(LCD_VDOT - 1 - ty);
#endif

    if(tx < 0) tx = 0;
    if(ty < 0) ty = 0;
    if(tx >= LCD_HDOT) tx = LCD_HDOT - 1;
    if(ty >= LCD_VDOT) ty = LCD_VDOT - 1;

    s_last_x = tx;
    s_last_y = ty;
    *x = tx;
    *y = ty;
    *pressed = 1;
}

/*---------------------------------------------------------------------------
 * LVGL input device
 *-------------------------------------------------------------------------*/
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    int16_t x, y;
    int pressed;

    (void)indev;
    board_touch_read(&x, &y, &pressed);

    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void lv_port_indev_init(void)
{
    lv_indev_t *indev = lv_indev_create();

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
}
