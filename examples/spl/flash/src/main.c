
#include <SWM341.h>
#include "synwit_board.h"
#include <stdio.h>

/* 注意地址必须为16个字节对齐的，且为扇区起始地址  */
#define FLASH_TEST_ADDR   (0x10000)    //400k
/* 注意不要超过256，即一个扇区的大小，否则需要先擦除后续的扇区即可 */
#define FLASH_TEST_BYTES  256

#if (FLASH_TEST_ADDR > (512<<10)) || (FLASH_TEST_ADDR%16 != 0) || \
		(FLASH_TEST_ADDR%4096 != 0)
#error "flash addr error!"
#endif

int main(void)
{
    uart0_init();
    delay_ms(1000);
    if(FLASH_RES_OK != FLASH_Erase(FLASH_TEST_ADDR)){
        printf("erase fail! addr: %x\n", FLASH_TEST_ADDR);
        while(1);
    }
    
    uint32_t tmp;
	/* flash控制器要求每次写入4个字（16个字节） */
    uint32_t w[4];
    for(uint32_t i = 0; i < FLASH_TEST_BYTES; i++){
        w[0] = w[1] = w[2] = w[3] = i;
        if(FLASH_RES_OK != FLASH_Write(FLASH_TEST_ADDR + sizeof(w)*i, w, 4)){
            printf("write fail! addr: %lx\n", FLASH_TEST_ADDR + i*sizeof(w));
        }
        tmp = *(volatile uint32_t *)(FLASH_TEST_ADDR + i*sizeof(w));
        printf("%lu: %lu :%s\n", i, tmp, i == tmp?"OK": "FAIL");
    }
    printf("test finish");
    for (;;){
    }
    return 0;
}
