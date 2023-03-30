
#include <SWM341.h>
#include "synwit_board.h"
#include "ff/source/ff.h"
#include <string.h>

FATFS fatfs;

const char hello_str[] = "hello world";

int main(void)
{
    uart0_init();
    FRESULT  res;
	FIL filw, filr;
    res = f_mount(&fatfs, "sd:", 1);
    if(res != FR_OK){
		printf("sdcard init fail!\r\n");
		while(1);
	}
    res = f_open(&filw, "sd:test.txt", FA_CREATE_ALWAYS | FA_WRITE);
	if(res != FR_OK){
		printf("create file fail!\r\n");
		while(1);
	}
    size_t len;
    res = f_write(&filw, hello_str, strlen(hello_str), &len);
    printf("write rest: %d\n", res);
    f_close(&filw);

    res = f_open(&filr, "sd:test.txt", FA_OPEN_EXISTING | FA_READ);
	if(res != FR_OK){
		printf("open file fail!\r\n");
		while(1);
	}
	char buf[64] = {0};
	res = f_read(&filr, buf, sizeof(buf)-1, &len);
    printf("read rest: %d len: %d\n", res, len);
	if(res != FR_OK){
		printf("read file fial!\r\n");
		while(1);
	}
	buf[len] = '\0';
    printf("read file: [%s] len: %d\n", buf, len);
	f_close(&filr);

    for (;;){
        delay_ms(1000);
        // printf("runing..\n");
    }
    return 0;
}
