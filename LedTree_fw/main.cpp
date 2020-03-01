#include "ch.h"
#include "hal.h"
#include "MsgQ.h"
#include "shell.h"
#include "uart.h"
#include "usb_msd.h"
#include "SimpleSensors.h"
#include "led.h"
#include "Sequences.h"
#include <vector>
#include "adcL476.h"

#if 1 // ======================== Variables & prototypes =======================
// Forever
bool OsIsInitialized = false;
EvtMsgQ_t<EvtMsg_t, MAIN_EVT_Q_LEN> EvtQMain;
static const UartParams_t CmdUartParams(115200, CMD_UART_PARAMS);
CmdUart_t Uart{&CmdUartParams};
void OnCmd(Shell_t *PShell);
void ITask();

bool UsbIsConnected = false;
static TmrKL_t TmrOneSecond {TIME_MS2I(999), evtIdEverySecond, tktPeriodic}; // Measure battery periodically
#endif

#if 1 // ==== LEDs ====
#define LED_FREQ_HZ     630
LedBlinker_t LedInd{LED_INDICATION};
std::vector<LedSmooth_t> Leds = {
        {LED1_PIN, LED_FREQ_HZ},
        {LED2_PIN, LED_FREQ_HZ},
        {LED3_PIN, LED_FREQ_HZ},
        {LED4_PIN, LED_FREQ_HZ},
        {LED5_PIN, LED_FREQ_HZ},
};
#endif // LEDs

#if 1 // ADC
void OnAdcDoneI();

const AdcSetup_t AdcSetup = {
        .SampleTime = ast24d5Cycles,
        .Oversampling = AdcSetup_t::oversmp8,
        .DoneCallback = OnAdcDoneI,
        .Channels = {
                {RESISTOR_PIN},
        }
};
#endif


int main(void) {
    // Setup clock frequency
    if(Clk.EnableHSE() == retvOk) {
        Clk.SetVoltageRange(mvrHiPerf);
        Clk.SetupFlashLatency(64, mvrHiPerf);
        Clk.SetupPllMulDiv(3, 32, 2, 4); // 12MHz / 3 = 4; 4*32 / 2 => 64
        Clk.SetupBusDividers(ahbDiv1, apbDiv1, apbDiv1);
        if(Clk.EnablePLL() == retvOk) {
            Clk.EnablePLLROut(); // main output
            Clk.SwitchToPLL();
        }
        // 48MHz clock for USB & 24MHz clock for ADC
        Clk.SetupPllSai1(8, 4, 2, 7); // 12 * 8 = 96; R = 96 / 4 = 24, Q = 96 / 2 = 48
        if(Clk.EnableSai1() == retvOk) {
            // Setup Sai1R as ADC source
            Clk.EnableSai1ROut();
            uint32_t tmp = RCC->CCIPR;
            tmp &= ~RCC_CCIPR_ADCSEL;
            tmp |= 0b01UL << 28; // SAI1R is ADC clock
            // Setup Sai1Q as 48MHz source
            Clk.EnableSai1QOut();
            tmp &= ~RCC_CCIPR_CLK48SEL;
            tmp |= ((uint32_t)src48PllSai1Q) << 26;
            RCC->CCIPR = tmp;
        }
    }
    Clk.UpdateFreqValues();
    // Init OS
    halInit();
    chSysInit();
    OsIsInitialized = true;

    // ==== Init hardware ====
    EvtQMain.Init();
    Uart.Init();
    Printf("\r%S %S\r", APP_NAME, XSTRINGIFY(BUILD_TIME));
    Clk.PrintFreqs();

    // Disable dualbank if enabled. Otherwise USB MSD will not be able to write flash.
    if(Flash::DualbankIsEnabled()) {
        Printf("Dualbank enabled, disabling\r");
        chThdSleepMilliseconds(45);
        Flash::DisableDualbank();   // Will reset inside
    }

    // ==== Leds ====
    LedInd.Init();
    LedInd.StartOrRestart(lsqIdle);
    for(LedSmooth_t &Led : Leds) {
        Led.Init();
    }

    UsbMsd.Init();
    SimpleSensors::Init();
    TmrOneSecond.StartOrRestart();
    // Inner ADC
    Adc.Init(AdcSetup);
    Adc.EnableVref();
    Adc.StartPeriodicMeasurement(10);

    // Main cycle
    ITask();
}

__noreturn
void ITask() {
    while(true) {
        EvtMsg_t Msg = EvtQMain.Fetch(TIME_INFINITE);
        switch(Msg.ID) {
            case evtIdShellCmd:
                OnCmd((Shell_t*)Msg.Ptr);
                ((Shell_t*)Msg.Ptr)->SignalCmdProcessed();
                LedInd.StartOrRestart(lsqCmd);
                break;

            case evtIdEverySecond:
//                Printf("Second\r");
                break;

            case evtIdADC:
                PrintfI("ADC: %u\r", Msg.Value);
                break;

#if 1       // ======= USB =======
            case evtIdUsbConnect:
                Printf("USB connect\r");
                UsbMsd.Connect();
                break;
            case evtIdUsbDisconnect:
                UsbMsd.Disconnect();
                Printf("USB disconnect\r");
                break;
            case evtIdUsbReady:
                Printf("USB ready\r");
                break;
#endif
            default: break;
        } // switch
    } // while true
}

void ProcessUsbDetect(PinSnsState_t *PState, uint32_t Len) {
    if((*PState == pssRising or *PState == pssHi) and !UsbIsConnected) {
        UsbIsConnected = true;
        EvtQMain.SendNowOrExit(EvtMsg_t(evtIdUsbConnect));
    }
    else if((*PState == pssFalling or *PState == pssLo) and UsbIsConnected) {
        UsbIsConnected = false;
        EvtQMain.SendNowOrExit(EvtMsg_t(evtIdUsbDisconnect));
    }
}

void OnAdcDoneI() {
    AdcBuf_t &FBuf = Adc.GetBuf();
    EvtQMain.SendNowOrExitI(EvtMsg_t(evtIdADC, FBuf[0]));
}

#if 1 // ======================= Command processing ============================
void OnCmd(Shell_t *PShell) {
	Cmd_t *PCmd = &PShell->Cmd;
    // Handle command
    if(PCmd->NameIs("Ping")) PShell->Ack(retvOk);
    else if(PCmd->NameIs("Version")) PShell->Print("%S %S\r", APP_NAME, XSTRINGIFY(BUILD_TIME));
    else if(PCmd->NameIs("mem")) PrintMemoryInfo();

    else if(PCmd->NameIs("Set")) {
        uint32_t indx, value;
        if(PCmd->GetNext<uint32_t>(&indx)  != retvOk or indx >= Leds.size()) { PShell->Ack(retvCmdError); return; }
        if(PCmd->GetNext<uint32_t>(&value) != retvOk) { PShell->Ack(retvCmdError); return; }
        Leds[indx].SetBrightness(value);
        PShell->Ack(retvOk);
    }

    else PShell->Ack(retvCmdUnknown);
}
#endif
