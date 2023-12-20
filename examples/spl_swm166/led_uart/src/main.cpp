
#include "SWM166.h"

static void SerialInit(void);


int main()
{
	SystemInit();
	SerialInit();

	GPIO_Init(GPIOA, PIN5, 1, 0, 0, 0);			//输出， 接LED
	GPIO_SetBit(GPIOA, PIN5);


	while(1);
}

void SerialInit(void)
{
	UART_InitStructure UART_initStruct;
	
	PORT_Init(PORTA, PIN0, PORTA_PIN0_UART0_RX, 1);	//GPIOA.0配置为UART0 RXD
	PORT_Init(PORTA, PIN1, PORTA_PIN1_UART0_TX, 0);	//GPIOA.1配置为UART0 TXD
 	
 	UART_initStruct.Baudrate = 57600;
	UART_initStruct.DataBits = UART_DATA_8BIT;
	UART_initStruct.Parity = UART_PARITY_NONE;
	UART_initStruct.StopBits = UART_STOP_1BIT;
	UART_initStruct.RXThreshold = 3;
	UART_initStruct.RXThresholdIEn = 0;
	UART_initStruct.TXThreshold = 3;
	UART_initStruct.TXThresholdIEn = 0;
	UART_initStruct.TimeoutTime = 10;
	UART_initStruct.TimeoutIEn = 0;
 	UART_Init(UART0, &UART_initStruct);
	UART_Open(UART0);
}


inline int putchar(int ch)    
{    
    UART_WriteByte(UART0, ch);    
    while(UART_IsTXBusy(UART0));    
    return ch;    
}    

#ifdef __GNUC__    
int _write(int fd, char *ptr, int len)    
{    
    for(int i = 0; i < len; i++){    
        putchar(ptr[i]);    
    }    
    return len;    
}    
#else    
int fputc(int ch, FILE *f)    
{    
    UART_WriteByte(UART0, ch);    
    while(UART_IsTXBusy(UART0));    
    return ch;    
}    
#endif