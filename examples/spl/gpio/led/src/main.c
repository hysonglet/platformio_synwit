#include "SWM341.h"

int main()
{
	SystemInit();

	GPIO_Init(GPIOC, PIN5, 1, 0, 0, 0);

	for(;;){
		GPIO_SetBit(GPIOC, PIN5);
		// GPIO_ClrBit(GPIOC, PIN5);
	}
	return 0;
}
