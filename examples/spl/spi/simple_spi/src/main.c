
#include <SWM341.h>
#include "synwit_board.h"
#include <string.h>

/* 测试时候短接M4和M5(mosi 和 miso)引脚即可测试spi回环效果 */
int main(void)
{
    uart0_init();
    spi0_init();
    const uint8_t hello[] = "hello";
    uint8_t recv[8] = {0};
    size_t recv_len;
    for (;;){
        // printf("%s\n","hello");
        delay_ms(1000);
        spi0_write_bytes(hello, strlen(hello));
        recv_len = spi0_read_bytes(recv, sizeof(recv), 10);
        // printf("\nrecv: %d", recv_len);
        for(int i = 0; i < recv_len; i++){
            printf("%c", recv[i]);
        }
    }
    return 0;
}
