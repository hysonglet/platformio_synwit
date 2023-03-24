#include "gt911.h"
#include "SWM341.h"
#include "synwit_board.h"
#include <stdio.h>

static bool gt911_i2c_read_regs(uint16_t reg, uint8_t *buf, int len)
{
    uint8_t i;
    uint8_t ack;

    ack = I2C_Start(I2C1, (GT911_I2C_ADDR) | 0, 1);
    if (ack == 0){
        goto rd_fail;
	}

    ack = I2C_Write(I2C1, reg >> 8, 1);
    if (ack == 0){
        goto rd_fail;
	}

    ack = I2C_Write(I2C1, reg & 0XFF, 1);
    if (ack == 0){
        goto rd_fail;
	}

	delay_ms(1);//此处必须延时等待一会再启动
	
    ack = I2C_Start(I2C1, (GT911_I2C_ADDR) | 1, 1); //ReStart
    if (ack == 0){
        goto rd_fail;
	}

    for (i = 0; i < len - 1; i++){
        buf[i] = I2C_Read(I2C1, 1, 1);
    }
    buf[i] = I2C_Read(I2C1, 0, 1);

    I2C_Stop(I2C1, 1);
    // delay_ms(1);
    return true;

rd_fail:
    I2C_Stop(I2C1, 1);
    // delay();
    return false;
}

static bool gt911_i2c_write_regs(uint16_t reg, uint8_t *buf, int len)
{
    uint32_t i;
    uint8_t ack;

    ack = I2C_Start(I2C1, (GT911_I2C_ADDR) | 0, 1);
    if (ack == 0){
        goto wr_fail;
	}

    ack = I2C_Write(I2C1, reg >> 8, 1);
    if (ack == 0){
        goto wr_fail;
	}

    ack = I2C_Write(I2C1, reg & 0XFF, 1);
    if (ack == 0){
        goto wr_fail;
	}

    for (i = 0; i < len; i++){
        ack = I2C_Write(I2C1, buf[i], 1);
        if (ack == 0)
            goto wr_fail;
    }

    I2C_Stop(I2C1, 1);
    // delay();
    return true;

wr_fail:
    I2C_Stop(I2C1, 1);
    // delay();
    return false;
}

void gt911_init(void)
{
    PORT_Init(GT911_I2C_SCL_PORT, GT911_I2C_SCL_PIN, GT911_I2C_SCL_PIN_FUN, 1);
    GT911_I2C_SCL_PORT->PULLU |= (1 << GT911_I2C_SCL_PIN);               //必须使能上拉
    GT911_I2C_SCL_PORT->OPEND |= (1 << GT911_I2C_SCL_PIN);	           //开漏
    PORT_Init(GT911_I2C_SDA_PORT, GT911_I2C_SDA_PIN, PORTC_PIN4_I2C1_SDA, 1); //配置为 I2C1 SDA 引脚
    GT911_I2C_SDA_PORT->PULLU |= (1 << GT911_I2C_SDA_PIN);
    GT911_I2C_SDA_PORT->OPEND |= (1 << GT911_I2C_SDA_PIN);

    I2C_InitStructure I2C_initStruct;
    I2C_initStruct.Master = 1;
    I2C_initStruct.Addr10b = 0;
    I2C_initStruct.MstClk = 400000;
	I2C_initStruct.TXEmptyIEn = 0;	 //发送寄存器空中断使能
	I2C_initStruct.RXNotEmptyIEn = 0;//接收寄存器非空中断使能
//	I2C_initStruct.SlvAddr = 0;
//	I2C_initStruct.SlvAddrMsk = 0;
//	I2C_initStruct.SlvSTADetIEn = 0;//从机检测到起始中断使能
//	I2C_initStruct.SlvSTODetIEn = 0;//从机检测到终止中断使能
    I2C_Init(I2C1, &I2C_initStruct);
    I2C_Open(I2C1);

    /*RESET = 1 INT = 0 */
#if (GT911_I2C_ADDR == GT911_I2C_ADDR_0XBA)
    GPIO_Init(GT911_INT_PORT, GT911_INT_PIN, 0, 0, 1, 0);
    /*RESET = 1 INT = 1 */
#elif (GT911_I2C_ADDR == GT911_I2C_ADDR_0X28)
    GPIO_Init(GT911_INT_PORT, GT911_INT_PIN, 0, 1, 0, 0);
#else
#error "I2C bus addr error"
#endif
    GPIO_Init(GT911_RESET_PORT, GT911_RESET_PIN, 1, 1, 0, 0);
    GPIO_ClrBit(GT911_RESET_PORT, GT911_RESET_PIN);
    delay_ms(1);
    GPIO_SetBit(GT911_RESET_PORT, GT911_RESET_PIN);
    delay_ms(10);

    gt911_config();
}

void gt911_config(void)
{
    uint8_t config;
    gt911_i2c_read_regs(GT911_REG_MODULE_SWITCH1_ADDR, &config, 1);
    config &= (~GT911_PRESSED_INT_MODEL_MASK);
    config |= GT911_PRESSED_INT_MODE;
    gt911_i2c_write_regs(GT911_REG_MODULE_SWITCH1_ADDR, &config, 1);
}

bool gt911_is_pressed(void)
{
#if (GT911_PRESSED_INT_MODE == 2)
    return GPIO_GetBit(GT911_INT_PORT, GT911_INT_PIN) == 1;
#elif (GT911_PRESSED_INT_MODE == 3)
    return GPIO_GetBit(GT911_INT_PORT, GT911_INT_PIN) == 0;
#else
#endif

}

bool gt911_read(tp_dev_t *tp_dev)
{
    // uint8_t buf[10] = {0};
    // bool ret;
    // ret = gt911_i2c_read_regs(GT911_REG_PODUCT_ID_ADDR, buf,GT911_REG_PODUCT_ID_LEN);
    // printf("ret: %d id: %s", ret ,buf);
    uint8_t temp_status = 0, touch_num = 0, temp = 0;
    uint8_t buf[GT911_MAX_TOUCH_CNT] = {0};

    gt911_i2c_read_regs(GT911_REG_GSTID_ADDR, &temp_status, 1);
    touch_num = temp_status & 0x0F;
    if ((temp_status & GT911_PRES_DOWN) && (touch_num <= GT911_MAX_TOUCH_CNT))
    {
        gt911_i2c_write_regs(GT911_REG_GSTID_ADDR, &temp, 1); // 清除READY标志
    }
    if ((0 != touch_num) && (touch_num <= GT911_MAX_TOUCH_CNT))
    {
        gt911_i2c_read_regs(GT911_REG_TP1_ADDR, buf, 4); //读取XY坐标值

        tp_dev->y[0] = (((uint16_t)buf[3] << 8) + buf[2]); // x 坐标偏移
		tp_dev->x[0] = (((uint16_t)buf[1] << 8) + buf[0]);		// y 坐标偏移
		
        tp_dev->status |= GT911_PRES_DOWN;//标记按键按下
    }
    else //if ((temp_status & 0x8F) == TP_PRES_DOWN) //无触摸点按下
    {
        tp_dev->x[0] = 0xFFFF;
        tp_dev->y[0] = 0xFFFF;
        tp_dev->status &= ~GT911_PRES_DOWN; //标记按键松开
    }
    return 0;
}

