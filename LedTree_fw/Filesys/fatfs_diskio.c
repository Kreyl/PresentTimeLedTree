/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2007        */
/*-----------------------------------------------------------------------*/
/* This is a stub disk I/O module that acts as front end of the existing */
/* disk I/O modules and attach it to FatFs module with common interface. */
/*-----------------------------------------------------------------------*/

#include "ch.h"
#include "hal.h"
#include "ffconf.h"
#include "diskio.h"
#include "string.h"

// KL
#undef HAL_USE_RTC

#include "mem_msd_glue.h"

extern void PrintfC(const char *format, ...);

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */

DSTATUS disk_initialize (
    BYTE drv                /* Physical drive nmuber (0..) */
)
{
    return 0;
}

/*-----------------------------------------------------------------------*/
/* Return Disk Status                                                    */

DSTATUS disk_status (
    BYTE drv        /* Physical drive nmuber (0..) */
)
{
    return 0;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
DRESULT disk_read (
    BYTE drv,        /* Physical drive nmuber (0..) */
    BYTE *buff,        /* Data buffer to store read data */
    DWORD sector,    /* Sector address (LBA) */
    BYTE count        /* Number of sectors to read (1..255) */
)
{
    MSDRead(sector, (uint32_t*)buff, count);
    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
static uint32_t IBuf[(FLASH_PAGE_SIZE / sizeof(uint32_t))];

#if _READONLY == 0
DRESULT disk_write (
    BYTE drv,            /* Physical drive nmuber (0..) */
    const BYTE *buff,    /* Data to be written */
    DWORD sector,        /* Sector address (LBA) */
    BYTE count           /* Number of sectors to write (1..255) */
)
{
    // Write data sector by sector
    while(count > 0) {
        memcpy(IBuf, buff, MSD_BLOCK_SZ); // Put data to definitely aligned buffer
        if(MSDWrite(sector, IBuf, 1) != 0) return RES_ERROR;
        sector++;
        buff += MSD_BLOCK_SZ;
        count--;
    }
    return RES_OK;
}
#endif /* _READONLY */

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */

DRESULT disk_ioctl (
    BYTE drv,        /* Physical drive nmuber (0..) */
    BYTE ctrl,        /* Control code */
    void *buff        /* Buffer to send/receive control data */
)
{
    switch (ctrl) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *((DWORD *)buff) = MSD_BLOCK_CNT;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *((WORD *)buff) = MSD_BLOCK_CNT;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *((DWORD *)buff) = 256; /* 512b blocks in one erase block */
        return RES_OK;
    default:
        return RES_PARERR;
    }
  return RES_PARERR;
}

DWORD get_fattime(void) {
    return ((uint32_t)0 | (1 << 16)) | (1 << 21); /* wrong but valid time */
}
