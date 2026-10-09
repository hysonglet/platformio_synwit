/*
 * FatFs <-> SWM341 SDIO glue: the disk I/O layer FatFs calls into.
 *
 * The card socket on the 5" board is wired 4 bits wide:
 *   SDCLK = M2, SDCMD = M4, SDD0/D1 = M5/M6, SDD2/D3 = N0/N1
 * None of those pins collides with the SDRAM bus (M12-M15, N2-N15).
 * SD_DET is not wired to the MCU (the schematic frees it up), so there is no
 * card-detect: presence is found out by talking to the card, which is why
 * board_sd_mount() can simply be retried when no card was in the slot.
 *
 * Ported from Synwit's SDIO/SDCard_FATFS demo and stripped to the SD drive.
 */
#include <string.h>

#include "SWM341.h"
#include "SEGGER_RTT.h"

#include "ff.h"       /* BYTE, LBA_t, UINT ... */
#include "diskio.h"   /* DSTATUS / DRESULT and the disk_* prototypes */

/* Bring-up traces: mounting talks to the card through a lot of layers, and the
 * CSL waits for the hardware without any timeout by default, so it helps to
 * see exactly which step stops answering. */
#define SD_TRACE(...) SEGGER_RTT_printf(0, __VA_ARGS__)

#define DEV_SD       0       /* the only volume there is (FF_VOLUMES == 1) */
#define SECTOR_SZ    512
#define SDIO_FREQ    10000000   /* 10 MHz - what Synwit's SDCard_FATFS uses */

static int      s_ready;                  /* SDIO_Init() succeeded */
static uint32_t s_bounce[SECTOR_SZ / 4];  /* 4-byte-aligned staging sector */

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == DEV_SD && s_ready) ? (DSTATUS)RES_OK : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if(pdrv != DEV_SD) return STA_NOINIT;

    PORT_Init(PORTM, PIN2, PORTM_PIN2_SD_CLK, 0);
    PORT_Init(PORTM, PIN4, PORTM_PIN4_SD_CMD, 1);
    PORT_Init(PORTM, PIN5, PORTM_PIN5_SD_D0,  1);
    PORT_Init(PORTM, PIN6, PORTM_PIN6_SD_D1,  1);
    PORT_Init(PORTN, PIN0, PORTN_PIN0_SD_D2,  1);
    PORT_Init(PORTN, PIN1, PORTN_PIN1_SD_D3,  1);

    /* SYSCLK 120 MHz -> SDIO base clock 30 MHz (SYS_CLKSEL_SDIO = SYSCLK/4) */
    SD_TRACE("SD: SDIO_Init(%u) ...\n", (unsigned)SDIO_FREQ);
    s_ready = (SDIO_Init(SDIO_FREQ) == SD_RES_OK);
    SD_TRACE("SD: SDIO_Init -> %s\n", s_ready ? "ok" : "failed");

    return s_ready ? (DSTATUS)RES_OK : STA_NOINIT;
}

/* SDIO reads and writes through uint32_t *, so the buffer has to be 4-byte
 * aligned - FatFs hands over whatever the caller owns, so bounce it. */
static DRESULT sd_transfer(int write, LBA_t sector, BYTE *buff, UINT count)
{
    uint32_t res;

    if(((uint32_t)buff & 3) != 0) {          /* unaligned: one sector a time */
        UINT i;

        for(i = 0; i < count; i++) {
            BYTE *cur = buff + (size_t)i * SECTOR_SZ;

            if(write) {
                memcpy(s_bounce, cur, SECTOR_SZ);
                res = SDIO_BlockWrite(sector + i, s_bounce);
            }
            else {
                res = SDIO_BlockRead(sector + i, s_bounce);
                if(res == SD_RES_OK) memcpy(cur, s_bounce, SECTOR_SZ);
            }
            if(res != SD_RES_OK) {
                SD_TRACE("SD: %s sector %u -> res %u\n",
                         write ? "write" : "read", (unsigned)(sector + i), (unsigned)res);
                return RES_ERROR;
            }
        }
        return RES_OK;
    }

    if(count == 1) {
        res = write ? SDIO_BlockWrite(sector, (uint32_t *)buff)
                    : SDIO_BlockRead (sector, (uint32_t *)buff);
    }
    else {
        res = write ? SDIO_MultiBlockWrite(sector, (uint16_t)count, (uint32_t *)buff)
                    : SDIO_MultiBlockRead (sector, (uint16_t)count, (uint32_t *)buff);
    }

    if(res != SD_RES_OK) {
        SD_TRACE("SD: %s %u block(s) at %u -> res %u\n",
                 write ? "write" : "read", (unsigned)count, (unsigned)sector, (unsigned)res);
    }

    return (res == SD_RES_OK) ? RES_OK : RES_ERROR;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    static int first = 1;

    if(first) {
        first = 0;
        SD_TRACE("SD: first read: sector %u, count %u\n", (unsigned)sector, (unsigned)count);
    }

    if(pdrv != DEV_SD || count == 0) return RES_PARERR;
    if(!s_ready) return RES_NOTRDY;

    return sd_transfer(0, sector, buff, count);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if(pdrv != DEV_SD || count == 0) return RES_PARERR;
    if(!s_ready) return RES_NOTRDY;

    return sd_transfer(1, sector, (BYTE *)buff, count);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if(pdrv != DEV_SD) return RES_PARERR;

    switch(cmd) {
    case CTRL_SYNC:
        return s_ready ? RES_OK : RES_NOTRDY;

    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = (LBA_t)(SD_cardInfo.CardCapacity / SECTOR_SZ);
        return s_ready ? RES_OK : RES_NOTRDY;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = SECTOR_SZ;
        return RES_OK;

    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
