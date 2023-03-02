#include "SWM341.h"

int main()
{
	SystemInit();

	GPIO_Init(GPIOA, PIN5, 1, 0, 0, 0);

	for(;;){
		GPIO_SetBit(GPIOA, PIN5);
	}
	return 0;
}
