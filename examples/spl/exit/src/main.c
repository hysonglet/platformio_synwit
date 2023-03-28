#include <SWM341.h>
#include "synwit_board.h"

int main()
{
    /* led A15 */
    led_init();
    /* key A14 */
    key_init();

    /* A14 */
    EXTI_Init(KEY_PORT, KEY_PIN, EXTI_BOTH_EDGE);

	NVIC_EnableIRQ(GPIOA_IRQn);
	
	EXTI_Open(KEY_PORT, KEY_PIN);

    for(;;){
    }
    return 0;
}

void GPIOA_Handler(void)
{
	if(EXTI_State(KEY_PORT, KEY_PIN)){
		EXTI_Clear(KEY_PORT, KEY_PIN);
        if(key_is_push() == true){
            led_on();
        }
        else{
            led_off();
        }
	}
}
