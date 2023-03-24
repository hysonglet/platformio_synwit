
#ifndef _GT911_H_
#define _GT911_H_

#include "SWM341.h"

#define GT911_INT_PORT              GPIOD
#define GT911_INT_PIN               PIN0
#define GT911_I2C_SDA_PORT          PORTC
#define GT911_I2C_SDA_PIN           PIN4
#define GT911_I2C_SDA_PIN_FUN       PORTC_PIN4_I2C1_SDA
#define GT911_I2C_SCL_PORT          PORTC
#define GT911_I2C_SCL_PIN           PIN5
#define GT911_I2C_SCL_PIN_FUN       PORTC_PIN5_I2C1_SCL
#define GT911_RESET_PORT            GPIOB
#define GT911_RESET_PIN             PIN11

#define GT911_I2C_ADDR_0XBA         (0xba)
#define GT911_I2C_ADDR_0X28         (0x28)

#define GT911_I2C_ADDR              GT911_I2C_ADDR_0XBA

#define GT911_PRES_DOWN 		(0x80) //触屏被按下
#define GT911_CATH_PRES 		(0x40) //有按键按下
#define GT911_MAX_TOUCH_CNT 		(5)    //电容屏支持的点数,固定为5点
/* 0: 上升沿触发  1： 下降沿触发  2: 低电平查询   3： 高电平查询*/
#define GT911_PRESSED_INT_MODE       3  
#define GT911_PRESSED_INT_MODEL_MASK 0x3 

typedef struct _tp
{
    uint16_t x[GT911_MAX_TOUCH_CNT]; //当前坐标
    uint16_t y[GT911_MAX_TOUCH_CNT]; //电容屏有最多5组坐标
    uint8_t status;           //笔的状态
                              //b7:按下1/松开0;
                              //b6~b5:保留
                              //b4~b0:电容触摸屏按下的点数(0,表示未按下,1表示按下)
}tp_dev_t;

#define GT911_REG_MODULE_SWITCH1_ADDR  0x804D
#define GT911_REG_PODUCT_ID_ADDR  0x8140
#define GT911_REG_PODUCT_ID_LEN   4
#define GT911_REG_GSTID_ADDR 		0x814E	//当前检测到的触摸情况
#define GT911_REG_TP1_ADDR 		0x8150	//第一个触摸点数据地址
#define GT911_REG_TP2_ADDR 		0x8158	//第二个触摸点数据地址
#define GT911_REG_TP3_ADDR 		0x8160	//第三个触摸点数据地址
#define GT911_REG_TP4_ADDR 		0x8168	//第四个触摸点数据地址
#define GT911_REG_TP5_ADDR 		0x8170	//第五个触摸点数据地址


void gt911_init(void);
void gt911_config(void);
bool gt911_read(tp_dev_t *tp);
bool gt911_is_pressed(void);


#endif
