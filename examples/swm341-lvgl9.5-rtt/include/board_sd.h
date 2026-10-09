#ifndef BOARD_SD_H
#define BOARD_SD_H

#include <stdint.h>

/* microSD socket on the 5" board, driven 4-bit wide through the SDIO
 * controller: SDCLK = M2, SDCMD = M4, SDD0/D1 = M5/M6, SDD2/D3 = N0/N1.
 * There is no card-detect line, so a missing card only shows up as a failed
 * mount - call board_sd_mount() again to pick up a card that was inserted
 * later.
 */
int  board_sd_mount(void);     /* pin mux + SDIO_Init + f_mount, 0 = ok, <0 = FRESULT */
int  board_sd_mounted(void);
void board_sd_unmount(void);

/* Capacity of the mounted volume in KB (512-byte sectors / 2). */
int  board_sd_info(uint32_t *total_kb, uint32_t *free_kb);

/* "FAT16" / "FAT32" / "exFAT" of the mounted volume. */
const char *board_sd_filesystem(void);

#endif /* BOARD_SD_H */
