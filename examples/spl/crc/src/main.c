
#include <SWM341.h>
#include "synwit_board.h"

#include <string.h>

int main(void)
{
    uart0_init();
    CRC_InitStructure CRC_initStruct;
    CRC_initStruct.Poly = CRC_POLY_104C11DB7;
	CRC_initStruct.init_crc = 0;
	CRC_initStruct.in_width = CRC_WIDTH_8;
	CRC_initStruct.in_rev = CRC_REV_NOT;
	CRC_initStruct.in_not = false;
	CRC_initStruct.out_rev = CRC_REV_NOT;
	CRC_initStruct.out_not = false;
	
	CRC_Init(CRC, &CRC_initStruct);

    const char test_str[] = "Hello World!";

    CRC_SetInitVal(CRC, 0);
    for(int i = 0; i < strlen(test_str); i++){
        CRC_Write(test_str[i]);
    }
    printf("CRC result: %lx\n", CRC_Result());

    for (;;){
    }
    return 0;
}
