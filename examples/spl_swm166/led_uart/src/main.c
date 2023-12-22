
#include "SWM166.h"

// static void SerialInit(void);

#define LED_PORT GPIOA
#define LED_PIN  PIN1

int main()
{
	SystemInit();
	// SerialInit();

	GPIO_Init(LED_PORT, LED_PIN, 1, 1, 1, 0);			//输出， 接LED
	GPIO_SetBit(LED_PORT, LED_PIN);
	
	while(1) {
	}
}

#if 0
void SerialInit(void)
{
	UART_InitStructure UART_initStruct;
	#define UART0_RX_PORT			PORTM
	#define UART0_RX_PIN			PIN4
	#define UART0_RX_PIN_FN			PORTM_PIN4_UART0_RX
	#define UART0_TX_PORT			PORTM
	#define UART0_TX_PIN			PIN3
	#define UART0_TX_PIN_FN			PORTM_PIN3_UART0_TX
	PORT_Init(UART0_RX_PORT, UART0_RX_PIN, UART0_RX_PIN_FN, 1);	//GPIOA.0配置为UART0 RXD
	PORT_Init(UART0_TX_PORT, UART0_TX_PIN, UART0_TX_PIN_FN, 0);	//GPIOA.1配置为UART0 TXD
 	
 	UART_initStruct.Baudrate = 115200;
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

#endif