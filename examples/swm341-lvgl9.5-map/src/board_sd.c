/*
 * microSD card on the 5" board: SDIO pins, then FatFs on top of it.
 *
 * The pin muxing and the SDIO_Init() call happen lazily inside FatFs'
 * disk_initialize() (see diskio_sdio.c), so mounting is all there is to it:
 * f_mount() with "immediate" set triggers the init the first time.
 */
#include "board_sd.h"

#include <string.h>

#include "SWM341.h"
#include "ff.h"

#include "board_lcd.h"   /* board_delay_ms() */

static FATFS s_fs;
static int   s_mounted;

static const char *fs_name(const FATFS *fs)
{
    switch(fs->fs_type) {
    case FS_FAT12: return "FAT12";
    case FS_FAT16: return "FAT16";
    case FS_FAT32: return "FAT32";
    case FS_EXFAT: return "exFAT";
    default:       return "unknown";
    }
}

const char *board_sd_filesystem(void)
{
    return s_mounted ? fs_name(&s_fs) : "-";
}

int board_sd_mounted(void)
{
    return s_mounted;
}

void board_sd_unmount(void)
{
    if(s_mounted) {
        f_unmount("sd:");
        s_mounted = 0;
    }
}

int board_sd_mount(void)
{
    FRESULT res;

    s_mounted = 0;

    /* the card needs a moment after power-up before it answers commands;
     * Synwit's demo waits SystemCoreClock/10 NOPs for the same reason. */
    board_delay_ms(100);

    res = f_mount(&s_fs, "sd:", 1);   /* 1 = mount now, i.e. talk to the card */
    if(res != FR_OK) return -(int)res;

    s_mounted = 1;
    return 0;
}

int board_sd_info(uint32_t *total_kb, uint32_t *free_kb)
{
    DWORD fre_clust;
    FATFS *fs;
    FRESULT res;

    if(!s_mounted) return -1;

    res = f_getfree("sd:", &fre_clust, &fs);
    if(res != FR_OK) return -(int)res;

    /* sectors per cluster * clusters = sectors; 2 sectors per KB */
    *total_kb = ((uint32_t)fs->n_fatent - 2) * fs->csize / 2;
    *free_kb  = (uint32_t)fre_clust * fs->csize / 2;

    return 0;
}
