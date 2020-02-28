/*
 * mem_msd_glue.h
 *
 *  Created on: 30 џэт. 2016 у.
 *      Author: Kreyl
 */

#pragma once

#include <inttypes.h>
#include "ch.h"

// Select target memory
#define MSD_USE_SD              FALSE
#define MSD_USE_EXT_SPI_FLASH   FALSE
#define MSD_USE_INNER_FLASH     TRUE

#if MSD_USE_SD
#include "kl_sd.h"
#include "diskio.h"
extern SDCDriver SDCD1;
#define MSD_BLOCK_CNT   SDCD1.capacity
#define MSD_BLOCK_SZ    512
#elif MSD_USE_EXT_SPI_FLASH
#define MSD_BLOCK_CNT   128 // Change this
#define MSD_BLOCK_SZ    512 // Change this

#elif MSD_USE_INNER_FLASH
#include "board.h"
#include "stm32_registry.h"
#define FLASH_START_ADDR        0x08000000
// Select page sz
#if defined STM32L476xx || defined STM32L433xx || defined STM32F10X_HD
#define FLASH_PAGE_SIZE         2048UL
#else
#define FLASH_PAGE_SIZE         1024UL
#endif
// Change this
#define MSD_STORAGE_SZ_KBYTES   128UL
#define MSD_FW_RESERVED_SZ_KBYTES   128UL
#define MSD_STORAGE_ADDR        (FLASH_START_ADDR + MSD_FW_RESERVED_SZ_KBYTES * 1024UL)
// Do not touch this
#define MSD_STORAGE_SZ_BYTES    (1024 * MSD_STORAGE_SZ_KBYTES)
#define MSD_BLOCK_CNT           (MSD_STORAGE_SZ_BYTES / FLASH_PAGE_SIZE)
#define MSD_BLOCK_SZ            FLASH_PAGE_SIZE
#endif

#ifdef __cplusplus
extern "C" {
#endif
uint8_t MSDRead(uint32_t BlockAddress, uint32_t *Ptr, uint32_t BlocksCnt);
uint8_t MSDWrite(uint32_t BlockAddress, uint32_t *Ptr, uint32_t BlocksCnt);
#ifdef __cplusplus
}
#endif
