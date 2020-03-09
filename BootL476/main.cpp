#include "hal.h"
#include "MsgQ.h"
#include "Sequences.h"
#include "shell.h"
#include "led.h"
#include "ff.h"
#include "kl_fs_utils.h"

#if 1 // =============== Low level ================
// Forever
static const UartParams_t CmdUartParams(115200, CMD_UART_PARAMS);
CmdUart_t Uart{&CmdUartParams};
EvtMsgQ_t<EvtMsg_t, MAIN_EVT_Q_LEN> EvtQMain;

LedBlinker_t Led(LED_PIN, omPushPull);

// Setup this
#define FILENAME_PATTERN        "Fw*.bin"
#define BOOTLOADER_RSRVD_SPACE  0x8000UL    // 32768 bytes for bootloader
#define TOTAL_FLASH_SZ          256000UL
#define PAGE_SZ                 2048L       // bytes in page, see datasheet
//// Do not touch
#define FLASH_START_ADDR        0x08000000UL
#define APP_START_ADDR          (FLASH_START_ADDR + BOOTLOADER_RSRVD_SPACE)
//#define APP_START_PAGE          (BOOTLOADER_RSRVD_SPACE / PAGE_SZ)
//#define TOTAL_PAGE_CNT          (TOTAL_FLASH_SZ / PAGE_SZ)
//#define APP_PAGE_CNT            ((TOTAL_FLASH_SZ - BOOTLOADER_RSRVD_SPACE) / PAGE_SZ)

void JumpToApp();
extern "C" { // used in fatfs_diskio.c
void ErasePage(uint32_t Addr);
void WriteToMemory(uint32_t Addr, uint32_t *PBuf, uint32_t Len);
}
static inline bool AppIsEmpty() {
    uint32_t FirstWord = *(uint32_t*)APP_START_ADDR;
    return (FirstWord == 0xFFFFFFFF);
}

void OnError() {
    if(AppIsEmpty()) {
        Led.StartOrRestart(lsqError); // Display error
        while(true);
    }
    else JumpToApp();
}

FATFS FlashFS;
uint32_t Buf[(PAGE_SZ / 4)];
#endif

int main(void) {
#if 1 // ==== Init ====
//    Iwdg::InitAndStart(4500);
//     Setup clock frequency
    Clk.SetCoreClk(cclk48MHz);
    Clk.UpdateFreqValues();

    // Init OS
    halInit();
    chSysInit();
    // ==== Init hardware ====
    Uart.Init();
    Printf("\r%S %S\r\n", APP_NAME, XSTRINGIFY(BUILD_TIME));
    Clk.PrintFreqs();

    // Disable dualbank if enabled
    if(Flash::DualbankIsEnabled()) {
        Printf("Dualbank enabled, disabling\r");
        chThdSleepMilliseconds(45);
        Flash::DisableDualbank();   // Will reset inside
    }

    Led.Init();
    Led.On();

    FRESULT err;
    err = f_mount(&FlashFS, "", 0);
    if(err != FR_OK) {
        Printf("FS error\r");
        chThdSleepMilliseconds(99);
        JumpToApp();
    }
#endif
#if 1 // =========================== Preparations ==============================
    // Try open file, jump to main app if not found
    if(f_findfirst(&Dir, &FileInfo, "", FILENAME_PATTERN) != FR_OK) {
        Printf("File search fail\r");
        chThdSleepMilliseconds(99);
        OnError();
    }
    if(FileInfo.fname[0] == 0) {
        Printf("%S not found\r", FILENAME_PATTERN);
        chThdSleepMilliseconds(99);
        OnError();
    }
    Printf("Found: %S\r", FileInfo.fname);
    if(TryOpenFileRead(FileInfo.fname, &CommonFile) != retvOk) OnError();
    int32_t TotalLen = f_size(&CommonFile);

    // Unlock flash
    chSysLock();
    Flash::LockFlash();
    Flash::UnlockFlash();
    chSysUnlock();
#endif
#if 1 // ======= Reading and flashing =======
    Led.StartOrRestart(lsqWriting);
    // ==== Read file block by block, do not write first page ====
    uint32_t BytesCnt, CurrentAddr = APP_START_ADDR;
    while(TotalLen != 0) {
        // Check flash address
        if(CurrentAddr >= (FLASH_START_ADDR + TOTAL_FLASH_SZ)) {
            Printf("Error: too large file\r");
            break;
        }

        // How many bytes to read?
        BytesCnt = MIN_(TotalLen, PAGE_SZ);
        TotalLen -= BytesCnt;
        // Read block
        if(f_read(&CommonFile, Buf, BytesCnt, &BytesCnt) != FR_OK) {
            Printf("Read error\r");
            OnError();
        }

        ErasePage(CurrentAddr);
        WriteToMemory(CurrentAddr, Buf, BytesCnt);
        CurrentAddr += BytesCnt;
    } // while
#endif
    Printf("\rWriting done\r");
    chThdSleepMilliseconds(99);
    f_close(&CommonFile);
    // Remove firmware file
    f_unlink(FileInfo.fname);
    chThdSleepMilliseconds(99);
//    Iwdg::Reload();
    chSysLock();
    Flash::LockFlash();
    chSysUnlock();
    JumpToApp();

    // Forever
    while(true);
}

void JumpToApp() {
    // Check if writing successfull
    if(AppIsEmpty()) {  // Unsuccessful, loop forever
        Led.StartOrRestart(lsqError);
        while(true);
    }
    Led.Stop();
    chSysLock();
    __disable_irq();
    // Start app
    void (*app)(void);
    uint32_t *p = (uint32_t*)APP_START_ADDR; // get a pointer to app
    SCB->VTOR = APP_START_ADDR; // offset the vector table
    __set_MSP(*p++);            // set main stack pointer to app Reset_Handler
    app = (void (*)(void))(*p);
    app();
    chSysUnlock();
}

extern "C" {
void ErasePage(uint32_t Addr) {
    uint32_t PageAddr = (Addr - FLASH_START_ADDR) / PAGE_SZ;
    if(Flash::ErasePage(PageAddr) != retvOk) {
        Printf("\rErase fail\r");
        chThdSleepMilliseconds(450);
        REBOOT();
    }
}
void WriteToMemory(uint32_t Addr, uint32_t *PBuf, uint32_t Len) {
    if(Flash::ProgramBuf32(Addr, PBuf, Len) != retvOk) {
        Printf("Write Fail\r");
        chThdSleepMilliseconds(450);
        REBOOT();
    }
    Printf(".");
}
} // extern C
