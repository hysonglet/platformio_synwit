#include <SWM341.h>
#include <synwit_board.h>


int main()
{
    led_init();
    key_init();
    for(;;){
        if(key_is_push() == true){
            led_turn();
            /* 等待释放 */
            while(key_is_push() == true);
        }
    }
    return 0;
}
