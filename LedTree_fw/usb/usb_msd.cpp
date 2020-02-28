/*
 * usb_cdc.cpp
 *
 *  Created on: 03 сент. 2015 г.
 *      Author: Kreyl
 */

#include "descriptors_msd.h"
#include <usb_msd.h>
#include "hal_usb.h"
#include "board.h"
#include "kl_lib.h"
#include "mem_msd_glue.h"
#include "kl_usb_defins.h"
#include "MsgQ.h"
#include "EvtMsgIDs.h"
#include "shell.h"

UsbMsd_t UsbMsd;
#define USBDrv          USBD1   // USB driver to use

#define EVT_USB_READY           EVENT_MASK(10)
#define EVT_USB_RESET           EVENT_MASK(11)
#define EVT_USB_SUSPEND         EVENT_MASK(12)
#define EVT_USB_CONNECTED       EVENT_MASK(13)
#define EVT_USB_DISCONNECTED    EVENT_MASK(14)
#define EVT_USB_IN_DONE         EVENT_MASK(15)
#define EVT_USB_OUT_DONE        EVENT_MASK(16)

static uint8_t SByte;
static bool ISayIsReady = true;
static thread_t *PMsdThd;

static bool OnSetupPkt(USBDriver *usbp);
static void OnDataInCompleted(USBDriver *usbp, usbep_t ep);
static void OnDataOutCompleted(USBDriver *usbp, usbep_t ep);

#if 1 // ========================== Endpoints ==================================
// ==== EP1 ====
static USBInEndpointState ep1instate;
static USBOutEndpointState ep1outstate;

// EP1 initialization structure (both IN and OUT).
static const USBEndpointConfig ep1config = {
    USB_EP_MODE_TYPE_BULK,
    NULL,                   // setup_cb
    OnDataInCompleted,      // in_cb
    OnDataOutCompleted,     // out_cb
    64,                     // in_maxsize
    64,                     // out_maxsize
    &ep1instate,            // in_state
    &ep1outstate,           // out_state
    2,                      // in_multiplier: Determines the space allocated for the TXFIFO as multiples of the packet size
    NULL                    // setup_buf: Pointer to a buffer for setup packets. Set this field to NULL for non-control endpoints
};
#endif

#if 1 // ============================ Events ===================================
static void usb_event(USBDriver *usbp, usbevent_t event) {
    switch (event) {
        case USB_EVENT_RESET:
            PrintfI("UsbRst\r");
            return;
        case USB_EVENT_ADDRESS:
            PrintfI("UsbAddr\r");
            return;
        case USB_EVENT_CONFIGURED: {
            chSysLockFromISR();
            /* Enable the endpoints specified in the configuration.
            Note, this callback is invoked from an ISR so I-Class functions must be used.*/
            usbInitEndpointI(usbp, EP_MSD_IN_ID,  &ep1config);
            usbInitEndpointI(usbp, EP_MSD_OUT_ID, &ep1config);
            ISayIsReady = true;
            EvtMsg_t Msg(evtIdUsbReady);
            EvtQMain.SendNowOrExitI(Msg);    // Signal to main thread
            chEvtSignalI(PMsdThd, EVT_USB_READY);
            chSysUnlockFromISR();
            return;
        } break;
        case USB_EVENT_SUSPEND:
        case USB_EVENT_WAKEUP:
        case USB_EVENT_STALLED:
        case USB_EVENT_UNCONFIGURED:
            return;
    } // switch
}
#endif

#if 1  // ==== USB driver configuration ====
const USBConfig UsbCfg = {
    usb_event,          // This callback is invoked when an USB driver event is registered
    GetDescriptor,      // Device GET_DESCRIPTOR request callback
    OnSetupPkt,         // This hook allows to be notified of standard requests or to handle non standard requests
    NULL                // Start Of Frame callback
};
#endif

/* ==== Setup Packet handler ====
 * true         Message handled internally.
 * false        Message not handled. */
bool OnSetupPkt(USBDriver *usbp) {
    SetupPkt_t *Setup = (SetupPkt_t*)usbp->setup;
//    Uart.PrintfI("%X %X %X %X %X\r", Setup->bmRequestType, Setup->bRequest, Setup->wValue, Setup->wIndex, Setup->wLength);
//    Uart.PrintfI("RT.Dir: %X; RT.Type: %X; RT.Rec: %X\r", Setup->ReqType.Direction, Setup->ReqType.Type, Setup->ReqType.Recipient);
    if(Setup->ReqType.Direction == DIR_DEV2HOST and
       Setup->ReqType.Type == TYPE_CLASS and
       Setup->ReqType.Recipient == RCPT_INTERFACE and
       Setup->bRequest == MS_REQ_GetMaxLUN and
       Setup->wLength == 1)
    {
//        Uart.PrintfI("MS_REQ_GetMaxLUN\r");
        SByte = 0;  // Maximum LUN ID
        usbSetupTransfer(usbp, &SByte, 1, NULL);
        return true;
    }

    if(Setup->ReqType.Direction == DIR_HOST2DEV and
       Setup->ReqType.Type == TYPE_CLASS and
       Setup->ReqType.Recipient == RCPT_INTERFACE and
       Setup->bRequest == MS_REQ_MassStorageReset and
       Setup->wLength == 0)
    {
//        Uart.PrintfI("MS_REQ_MassStorageReset\r");
        // TODO: remove Stall condition
        return true; // Acknowledge reception
    }

    return false;
}

void OnDataInCompleted(USBDriver *usbp, usbep_t ep) {
    chSysLockFromISR();
//    Uart.PrintfI("inDone\r");
    chEvtSignalI(PMsdThd, EVT_USB_IN_DONE);
    chSysUnlockFromISR();
}

void OnDataOutCompleted(USBDriver *usbp, usbep_t ep) {
    chSysLockFromISR();
//    Uart.PrintfI("OutDone\r");
    chEvtSignalI(PMsdThd, EVT_USB_OUT_DONE);
    chSysUnlockFromISR();
}


#if 1 // ========================== MSD Thread =================================
static MS_CommandBlockWrapper_t CmdBlock;
static MS_CommandStatusWrapper_t CmdStatus;
static SCSI_RequestSenseResponse_t SenseData;
static SCSI_ReadCapacity10Response_t ReadCapacity10Response;
static SCSI_ReadFormatCapacitiesResponse_t ReadFormatCapacitiesResponse;
static uint32_t Buf32[(MSD_DATABUF_SZ/4)];

static void SCSICmdHandler();
// Scsi commands
static void CmdTestReady();
static uint8_t CmdStartStopUnit();
static uint8_t CmdInquiry();
static uint8_t CmdRequestSense();
static uint8_t CmdReadCapacity10();
static uint8_t CmdSendDiagnostic();
static uint8_t CmdReadFormatCapacities();
static uint8_t CmdRead10();
static uint8_t CmdWrite10();
static uint8_t CmdModeSense6();
static uint8_t ReadWriteCommon(uint32_t *PAddr, uint16_t *PLen);
static void BusyWaitIN();
static uint8_t BusyWaitOUT();

static THD_WORKING_AREA(waUsbThd, 256);
static THD_FUNCTION(UsbThd, arg) {
    chRegSetThreadName("Usb");
    while(true) {
        uint32_t EvtMsk = chEvtWaitAny(ALL_EVENTS);
        if(EvtMsk & EVT_USB_READY) {
            // Receive header
            chSysLock();
            usbStartReceiveI(&USBDrv, EP_MSD_OUT_ID, (uint8_t*)&CmdBlock, MS_CMD_SZ);
            chSysUnlock();
        }

        if(EvtMsk & EVT_USB_OUT_DONE) {
            SCSICmdHandler();
            // Receive header again
            chSysLock();
            usbStartReceiveI(&USBDrv, EP_MSD_OUT_ID, (uint8_t*)&CmdBlock, MS_CMD_SZ);
            chSysUnlock();
        }
    } // while true
}
#endif

void UsbMsd_t::Init() {
#if defined STM32L4XX
    PinSetupAlterFunc(USB_DM, omPushPull, pudNone, USB_AF, psVeryHigh);
    PinSetupAlterFunc(USB_DP, omPushPull, pudNone, USB_AF, psVeryHigh);

    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    // Connect VddUSB
    PWR->CR2 |= PWR_CR2_USV;
#else
    PinSetupAnalog(GPIOA, 11);
    PinSetupAnalog(GPIOA, 12);
#endif
    // Variables
    SenseData.ResponseCode = 0x70;
    SenseData.AddSenseLen = 0x0A;
    // Thread
    PMsdThd = chThdCreateStatic(waUsbThd, sizeof(waUsbThd), NORMALPRIO, (tfunc_t)UsbThd, NULL);
    usbInit();
}

void UsbMsd_t::Reset() {
    // Wake thread if sleeping
    chSysLock();
    if(PMsdThd->state == CH_STATE_SUSPENDED) chSchReadyI(PMsdThd);
    chSysUnlock();
}

void UsbMsd_t::Connect() {
    usbDisconnectBus(&USBDrv);
    chThdSleepMilliseconds(500);
    usbStart(&USBDrv, &UsbCfg);
    usbConnectBus(&USBDrv);
}
void UsbMsd_t::Disconnect() {
    usbDisconnectBus(&USBDrv);
    usbStop(&USBDrv);
}

void TransmitBuf(uint32_t *Ptr, uint32_t Len) {
    chSysLock();
    usbStartTransmitI(&USBDrv, EP_MSD_IN_ID, (uint8_t*)Ptr, Len);
    chSysUnlock();
    BusyWaitIN();
}

uint8_t ReceiveToBuf(uint32_t *Ptr, uint32_t Len) {
    chSysLock();
    usbStartReceiveI(&USBDrv, EP_MSD_IN_ID, (uint8_t*)Ptr, Len);
    chSysUnlock();
    return BusyWaitOUT();
}

void BusyWaitIN() {
    chEvtWaitAny(ALL_EVENTS);
}

uint8_t BusyWaitOUT() {
    eventmask_t evt = chEvtWaitAnyTimeout(EVT_USB_OUT_DONE, TIME_MS2I(MSD_TIMEOUT_MS));
    return (evt == 0)? retvTimeout : retvOk;
}

#if 1 // =========================== SCSI ======================================
//#define DBG_PRINT_CMD   TRUE
void SCSICmdHandler() {
//    Uart.Printf("Sgn=%X; Tag=%X; Len=%u; Flags=%X; LUN=%u; SLen=%u; SCmd=%A\r", CmdBlock.Signature, CmdBlock.Tag, CmdBlock.DataTransferLen, CmdBlock.Flags, CmdBlock.LUN, CmdBlock.SCSICmdLen, CmdBlock.SCSICmdData, CmdBlock.SCSICmdLen, ' ');
//    Printf("SCmd=%A\r", CmdBlock.SCSICmdData, CmdBlock.SCSICmdLen, ' ');
    uint8_t CmdRslt = retvFail;
    switch(CmdBlock.SCSICmdData[0]) {
        case SCSI_CMD_TEST_UNIT_READY:    CmdTestReady();     return; break;    // Will report itself
        case SCSI_CMD_START_STOP_UNIT:    CmdRslt = CmdStartStopUnit(); break;
        case SCSI_CMD_INQUIRY:            CmdRslt = CmdInquiry(); break;
        case SCSI_CMD_REQUEST_SENSE:      CmdRslt = CmdRequestSense(); break;
        case SCSI_CMD_READ_CAPACITY_10:   CmdRslt = CmdReadCapacity10(); break;
        case SCSI_CMD_SEND_DIAGNOSTIC:    CmdRslt = CmdSendDiagnostic(); break;
        case SCSI_READ_FORMAT_CAPACITIES: CmdRslt = CmdReadFormatCapacities(); break;
        case SCSI_CMD_WRITE_10:           CmdRslt = CmdWrite10(); break;
        case SCSI_CMD_READ_10:            CmdRslt = CmdRead10(); break;
        case SCSI_CMD_MODE_SENSE_6:       CmdRslt = CmdModeSense6(); break;
        // These commands should just succeed, no handling required
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        case SCSI_CMD_VERIFY_10:
        case SCSI_CMD_SYNCHRONIZE_CACHE_10:
            CmdRslt = retvOk;
            CmdBlock.DataTransferLen = 0;
            break;
        default:
            Printf("MSCmd %X not supported\r", CmdBlock.SCSICmdData[0]);
            // Update the SENSE key to reflect the invalid command
            SenseData.SenseKey = SCSI_SENSE_KEY_ILLEGAL_REQUEST;
            SenseData.AdditionalSenseCode = SCSI_ASENSE_INVALID_COMMAND;
            SenseData.AdditionalSenseQualifier = SCSI_ASENSEQ_NO_QUALIFIER;
            break;
    } // switch
    // Update Sense if command was successfully processed
    if(CmdRslt == retvOk) {
        SenseData.SenseKey = SCSI_SENSE_KEY_GOOD;
        SenseData.AdditionalSenseCode = SCSI_ASENSE_NO_ADDITIONAL_INFORMATION;
        SenseData.AdditionalSenseQualifier = SCSI_ASENSEQ_NO_QUALIFIER;
    }

    // Send status
    CmdStatus.Signature = MS_CSW_SIGNATURE;
    CmdStatus.Tag = CmdBlock.Tag;
    if(CmdRslt == retvOk) {
        CmdStatus.Status = SCSI_STATUS_OK;
        // DataTransferLen decreased at cmd handlers
        CmdStatus.DataTransferResidue = CmdBlock.DataTransferLen;
    }
    else {
        CmdStatus.Status = SCSI_STATUS_CHECK_CONDITION;
        CmdStatus.DataTransferResidue = 0;    // 0 or requested length?
    }

    // Stall if cmd failed and there is data to send
//    bool ShouldSendStatus = true;
//    if((CmdRslt != retvOk)) {
//        chSysLock();
//        ShouldSendStatus = !usbStallTransmitI(&USBDrv, EP_DATA_IN_ID);  // transmit status if successfully stalled
//        chSysUnlock();
//    }
//    if(ShouldSendStatus) {
        TransmitBuf((uint32_t*)&CmdStatus, sizeof(MS_CommandStatusWrapper_t));
//    }
}

void CmdTestReady() {
#if DBG_PRINT_CMD
    Printf("CmdTestReady (Rdy: %u)\r", ISayIsReady);
#endif
    CmdBlock.DataTransferLen = 0;
    CmdStatus.Signature = MS_CSW_SIGNATURE;
    CmdStatus.Tag = CmdBlock.Tag;
    CmdStatus.DataTransferResidue = CmdBlock.DataTransferLen;

    if(ISayIsReady) {
        CmdStatus.Status = SCSI_STATUS_OK;
        SenseData.SenseKey = SCSI_SENSE_KEY_GOOD;
        SenseData.AdditionalSenseCode = SCSI_ASENSE_NO_ADDITIONAL_INFORMATION;
        SenseData.AdditionalSenseQualifier = SCSI_ASENSEQ_NO_QUALIFIER;
    }
    else {
        CmdStatus.Status = SCSI_STATUS_CHECK_CONDITION;
        SenseData.SenseKey = SCSI_SENSE_KEY_NOT_READY;
        SenseData.AdditionalSenseCode = SCSI_ASENSE_MEDIUM_NOT_PRESENT;
        SenseData.AdditionalSenseQualifier = SCSI_ASENSEQ_NO_QUALIFIER;
    }

    TransmitBuf((uint32_t*)&CmdStatus, sizeof(MS_CommandStatusWrapper_t));
}

uint8_t CmdStartStopUnit() {
#if DBG_PRINT_CMD
    Printf("CmdStartStopUnit [4]=%02X\r", CmdBlock.SCSICmdData[4]);
#endif
    if((CmdBlock.SCSICmdData[4] & 0x03) == 0x02) {  // Eject
        ISayIsReady = false;
    }
    else if((CmdBlock.SCSICmdData[4] & 0x03) == 0x03) {  // Load
        ISayIsReady = true;
    }
    return retvOk;
}

uint8_t CmdInquiry() {
#if DBG_PRINT_CMD
    Printf("CmdInquiry %u\r", CmdBlock.SCSICmdData[1] & 0x01);
#endif
    uint16_t RequestedLength = Convert::BuildUint16(CmdBlock.SCSICmdData[4], CmdBlock.SCSICmdData[3]);
    uint16_t BytesToTransfer;
    if(CmdBlock.SCSICmdData[1] & 0x01) { // Evpd is set
        BytesToTransfer = MIN_(RequestedLength, PAGE0_INQUIRY_DATA_SZ);
        TransmitBuf((uint32_t*)&Page00InquiryData, BytesToTransfer);
    }
    else {
        // Transmit InquiryData
        BytesToTransfer = MIN_(RequestedLength, sizeof(SCSI_InquiryResponse_t));
        TransmitBuf((uint32_t*)&InquiryData, BytesToTransfer);
    }
    // Succeed the command and update the bytes transferred counter
    CmdBlock.DataTransferLen -= BytesToTransfer;
    return retvOk;
}
uint8_t CmdRequestSense() {
#if DBG_PRINT_CMD
    Printf("CmdRequestSense\r");
#endif
    uint16_t RequestedLength = CmdBlock.SCSICmdData[4];
    uint16_t BytesToTransfer = MIN_(RequestedLength, sizeof(SenseData));
    // Transmit SenceData
    TransmitBuf((uint32_t*)&SenseData, BytesToTransfer);
    // Succeed the command and update the bytes transferred counter
    CmdBlock.DataTransferLen -= BytesToTransfer;
    return retvOk;
}
uint8_t CmdReadCapacity10() {
#if DBG_PRINT_CMD
    Printf("CmdReadCapacity10\r");
#endif
    ReadCapacity10Response.LastBlockAddr = __REV((uint32_t)MSD_BLOCK_CNT - 1);
    ReadCapacity10Response.BlockSize = __REV((uint32_t)MSD_BLOCK_SZ);
    // Transmit SenceData
    TransmitBuf((uint32_t*)&ReadCapacity10Response, sizeof(ReadCapacity10Response));
    // Succeed the command and update the bytes transferred counter
    CmdBlock.DataTransferLen -= sizeof(ReadCapacity10Response);
    return retvOk;
}
uint8_t CmdSendDiagnostic() {
    Printf("CmdSendDiagnostic\r");
    return retvCmdUnknown;
}
uint8_t CmdReadFormatCapacities() {
#if DBG_PRINT_CMD
    Printf("CmdReadFormatCapacities\r");
#endif
    ReadFormatCapacitiesResponse.Length = 0x08;
    ReadFormatCapacitiesResponse.NumberOfBlocks = __REV(MSD_BLOCK_CNT);
    // 01b Unformatted Media - Maximum formattable capacity for this cartridge
    // 10b Formatted Media - Current media capacity
    // 11b No Cartridge in Drive - Maximum formattable capacity
    ReadFormatCapacitiesResponse.DescCode = 0x02;
    ReadFormatCapacitiesResponse.BlockSize[0] = (uint8_t)((uint32_t)MSD_BLOCK_SZ >> 16);
    ReadFormatCapacitiesResponse.BlockSize[1] = (uint8_t)((uint32_t)MSD_BLOCK_SZ >> 8);
    ReadFormatCapacitiesResponse.BlockSize[2] = (uint8_t)((uint32_t)MSD_BLOCK_SZ);
    // Transmit Data
    TransmitBuf((uint32_t*)&ReadFormatCapacitiesResponse, sizeof(ReadFormatCapacitiesResponse));
    // Succeed the command and update the bytes transferred counter
    CmdBlock.DataTransferLen -= sizeof(ReadFormatCapacitiesResponse);
    return retvOk;
}

uint8_t ReadWriteCommon(uint32_t *PAddr, uint16_t *PLen) {
    *PAddr = Convert::BuildUint32(CmdBlock.SCSICmdData[5], CmdBlock.SCSICmdData[4], CmdBlock.SCSICmdData[3], CmdBlock.SCSICmdData[2]);
    *PLen  = Convert::BuildUint16(CmdBlock.SCSICmdData[8], CmdBlock.SCSICmdData[7]);
//    Uart.Printf("Addr=%u; Len=%u\r", *PAddr, *PLen);
    // Check block addr
    if((*PAddr + *PLen) > MSD_BLOCK_CNT) {
        Printf("Out Of Range: Addr %u, Len %u\r", *PAddr, *PLen);
        SenseData.SenseKey = SCSI_SENSE_KEY_ILLEGAL_REQUEST;
        SenseData.AdditionalSenseCode = SCSI_ASENSE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
        SenseData.AdditionalSenseQualifier = SCSI_ASENSEQ_NO_QUALIFIER;
        return retvFail;
    }
    // Check cases 4, 5: (Hi != Dn); and 3, 11, 13: (Hn, Ho != Do)
    if(CmdBlock.DataTransferLen != (*PLen) * MSD_BLOCK_SZ) {
        Printf("Wrong length\r");
        SenseData.SenseKey = SCSI_SENSE_KEY_ILLEGAL_REQUEST;
        SenseData.AdditionalSenseCode = SCSI_ASENSE_INVALID_COMMAND;
        SenseData.AdditionalSenseQualifier = SCSI_ASENSEQ_NO_QUALIFIER;
        return retvFail;
    }
    return retvOk;
}

uint8_t CmdRead10() {
#if DBG_PRINT_CMD
    Printf("CmdRead10\r");
#endif
    uint32_t BlockAddress=0;
    uint16_t TotalBlocks=0;
    if(ReadWriteCommon(&BlockAddress, &TotalBlocks) != retvOk) return retvFail;
    // ==== Send data ====
    uint32_t BlocksToRead, BytesToSend; // Intermediate values
    bool Rslt;
    while(TotalBlocks != 0) {
        BlocksToRead = MIN_(MSD_DATABUF_SZ / MSD_BLOCK_SZ, TotalBlocks);
        BytesToSend = BlocksToRead * MSD_BLOCK_SZ;
        Rslt = MSDRead(BlockAddress, Buf32, BlocksToRead);
//        Uart.Printf("%A\r", Buf, 50, ' ');
        if(Rslt == retvOk) {
            TransmitBuf(Buf32, BytesToSend);
            CmdBlock.DataTransferLen -= BytesToSend;
            TotalBlocks  -= BlocksToRead;
            BlockAddress += BlocksToRead;
        }
        else {
            Printf("Rd fail\r");
            // TODO: handle read error
            return retvFail;
        }
    } // while
    return retvOk;
}

uint8_t CmdWrite10() {
#if DBG_PRINT_CMD
    Printf("CmdWrite10\r");
#endif
#if READ_ONLY
    SenseData.SenseKey = SCSI_SENSE_KEY_DATA_PROTECT;
    SenseData.AdditionalSenseCode = SCSI_ASENSE_WRITE_PROTECTED;
    SenseData.AdditionalSenseQualifier = SCSI_ASENSEQ_NO_QUALIFIER;
    return retvFail;
#else
    // Check case 8: Hi != Do
    if(CmdBlock.Flags & 0x80) {
        SenseData.SenseKey = SCSI_SENSE_KEY_ILLEGAL_REQUEST;
        SenseData.AdditionalSenseCode = SCSI_ASENSE_INVALID_COMMAND;
        return retvFail;
    }
    // TODO: Check if ready
    if(false) {
        SenseData.SenseKey = SCSI_SENSE_KEY_NOT_READY;
        SenseData.AdditionalSenseCode = SCSI_ASENSE_MEDIUM_NOT_PRESENT;
        return retvFail;
    }
    uint32_t BlockAddress=0;
    uint16_t TotalBlocks=0;
    // Get transaction size
    if(ReadWriteCommon(&BlockAddress, &TotalBlocks) != retvOk) return retvFail;
//    Uart.Printf("Addr=%u; Len=%u\r", BlockAddress, TotalBlocks);
    uint32_t BlocksToWrite, BytesToReceive;
    uint8_t Rslt = retvOk;

    while(TotalBlocks != 0) {
        // Fill Buf1
        BytesToReceive = MIN_(MSD_DATABUF_SZ, TotalBlocks * MSD_BLOCK_SZ);
        BlocksToWrite  = BytesToReceive / MSD_BLOCK_SZ;
        if(ReceiveToBuf(Buf32, BytesToReceive) != retvOk) {
            Printf("Rcv fail\r");
            return retvFail;
        }
        // Write Buf to memory
        Rslt = MSDWrite(BlockAddress, Buf32, BlocksToWrite);
        if(Rslt != retvOk) {
            Printf("Wr fail\r");
            return retvFail;
        }
        CmdBlock.DataTransferLen -= BytesToReceive;
        TotalBlocks -= BlocksToWrite;
        BlockAddress += BlocksToWrite;
    } // while
    return retvOk;
#endif
}

uint8_t CmdModeSense6() {
#if DBG_PRINT_CMD
    Printf("CmdModeSense6\r");
#endif
    uint16_t RequestedLength = CmdBlock.SCSICmdData[4];
    uint16_t BytesToTransfer = MIN_(RequestedLength, MODE_SENSE6_DATA_SZ);
    TransmitBuf((uint32_t*)&Mode_Sense6_data, BytesToTransfer);
    // Succeed the command and update the bytes transferred counter
    CmdBlock.DataTransferLen -= BytesToTransfer;
    return retvOk;
}
#endif
