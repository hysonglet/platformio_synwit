#include "synwit_board.h"

class Led{
	/* LED : PA15 */
public:
	Led(void){};
	~Led(void){};

public:
	void init(void){
		led_init();
	}
	void on(void){
		led_on();
	}
	void off(void){
		led_off();
	}
	void trun(void){
		led_turn();
	}
};

int main()
{
	Led led;
	led.init();
	for(;;){
		led.trun();
		delay_ms(500);
	}
	return 0;
}
