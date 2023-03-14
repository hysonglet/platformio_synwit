
#include <SWM341.h>
#include "synwit_board.h"
#include <stdio.h>

int main(void)
{
    int cnt = 0;
    uart0_init();
    
    printf("%s %d\n", "Hello Synwit.", cnt++);

    printf("test sdram write & read for byte\n");
    for(size_t i = 0; i < 0xff; i += sizeof(uint8_t)){
        uint8_t *ptr = (volatile uint8_t *)(SDRAMM_BASE + 0);
        *(ptr + i) = i;
        printf("addr: %x write %x, read: %x\n", SDRAMM_BASE + i*sizeof(i), 
            i, (uint8_t)*(ptr + i));
    }
    printf("test sdram write & read for half word\n");
    for(size_t i = 0; i < 0x1000; i += sizeof(uint16_t)){
        uint16_t *ptr = (volatile uint16_t *)(SDRAMM_BASE + 0);
        *(ptr + i) = i;
        printf("addr: %x write %x, read: %x\n", SDRAMM_BASE + i*sizeof(i), 
            i, (uint16_t)*(ptr + i));
    }
    printf("test sdram write & read for word\n");
    for(size_t i = 0; i < 0x10000; i += sizeof(uint32_t)){
        uint32_t *ptr = (volatile uint32_t *)(SDRAMM_BASE + 0);
        *(ptr + i) = i;
        printf("addr: %x write %x, read: %x\n", SDRAMM_BASE + i*sizeof(i), 
            i, (uint32_t)*(ptr + i));
    }

    while(0);
    return 0;
}
