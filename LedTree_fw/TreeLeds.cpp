/*
 * TreeLeds.cpp
 *
 *  Created on: 5 мар. 2020 г.
 *      Author: layst
 */

#include "TreeLeds.h"
#include "board.h"
#include "kl_lib.h"
#include <vector>
#include "Settings.h"
#include "shell.h"

#define LED_FREQ_HZ     630

#if 1 // ==== Leds Q ====
EvtMsgQ_t<EvtMsg_t, 18> LedsMsgQ;

#define LEDS_SET_BRT_CMD            4
#define LEDS_CONSTRUCT_PROFILE_CMD  7
#endif

#if 1 // ============ LED class =============
enum Stage_t {stgPause, stg1, stg2};

float TPeriodLeft[LEDS_CNT];
float TPeriodGood[LEDS_CNT];

class BigLed_t;
extern std::vector<BigLed_t> Leds;

/* === Profile ===
           VMax
          /\
         /  \
        /    \
 VStart/      \ Vend
       TPeriod
*/

class BigLed_t {
private:
    const PinOutputPWM_t IChnl;
//    const int32_t IIndx;
    float ICurrentValue;
    const uint32_t PWMFreq;
    float CurrBrt = LED_SMOOTH_MAX_BRT;
    void SetCurrent() {
        // CurrBrt=[0;LED_SMOOTH_MAX_BRT]; ICurrentValue=[0;255]
        float x = ICurrentValue;
        float y = 0.000012*x*x*x + 0.00097*x*x - 0.035*x + 0.315;
        y = y  * CurrBrt;
        IChnl.Set((int32_t)y);
//        Printf("Curr: %f\r", y);
    }
    // Profile
    Stage_t Stage;
    float VMax, VEnd;
    float Tick, TickEnd;
    float a1, b1, a2, b2;
    void IConstructProfValues() {
        uint32_t HalfValue = Settings.MinValue + (Settings.MaxValue - Settings.MinValue) / 2;
        VMax = Random::Generate(HalfValue, Settings.MaxValue);
        VEnd = Random::Generate(Settings.MinValue, HalfValue);
    }
public:
    float TPeriod;
    BigLed_t(const PwmSetup_t APinSetup, const uint32_t AFreq = 0xFFFFFFFF) :
        IChnl(APinSetup), ICurrentValue(0), PWMFreq(AFreq) {}
    void Init() {
        IChnl.Init();
        IChnl.SetFrequencyHz(PWMFreq);
        Set(0);
    }
    void SetBrightness(uint32_t NewBrt) {
        CurrBrt = NewBrt;
        SetCurrent();
    }
    void Set(int32_t AValue) {
        ICurrentValue = AValue;
        SetCurrent();
    }

    void ConstructAndStartProfile() {
//        Printf("%S\r", __FUNCTION__);
        Stage = stg1;
        Tick = 0;
        float VStart = VEnd;
        IConstructProfValues(); // As always
        // Get min and max times left
        bool AllAreOut = true;
        for(uint32_t i=0; i<Leds.size(); i++) {
            Printf("%d; ", (int32_t)TPeriodLeft[i]);
            if(TPeriodLeft[i] >= (Settings.MinPeriod / 2) and TPeriodLeft[i] <= (Settings.MaxPeriod / 2)) {
                AllAreOut = false;
                break;
            }
        }
        if(AllAreOut) {
            TPeriod = Random::Generate(Settings.MinPeriod, Settings.MaxPeriod);
            Printf("T1: %d\r", (int32_t)TPeriod);
        }
        else {
            // Get good periods
            float *p = TPeriodGood;
            for(uint32_t i=0; i<Leds.size(); i++) {
                float Per = 2 * TPeriodLeft[i];
                if(Per >= Settings.MinPeriod and Per <= Settings.MaxPeriod) {
                    *p++ = Per;
                }
            }
            // Select random good period
            uint32_t GoodPerCnt = p - TPeriodGood;
            int32_t Indx = Random::Generate(0, GoodPerCnt-1);
            TPeriod = TPeriodGood[Indx];
            Printf("T2: %d\r", (int32_t)TPeriod);
        }
        // Calculate coeffs
        a1 = 2 * (VMax - VStart) / TPeriod;
        b1 = VStart;
        a2 = 2 * (VEnd - VMax) / TPeriod;
        b2 = VMax;
    }

    void ConstructAndStartFirstProfile() {
        Stage = stgPause;
        Tick = 0;
        IConstructProfValues(); // As always
        TickEnd = Random::Generate(0, Settings.TurnOnMaxPause);
        TPeriod = Random::Generate(Settings.MinPeriod, Settings.MaxPeriod);
        a1 = 2 * VMax / TPeriod;
        b1 = 0;
        a2 = 2 * (VEnd - VMax) / TPeriod;
        b2 = VMax;
        ICurrentValue = 0; // Off initially
        SetCurrent();
    }

    void OnTick() {
        Tick++;
        TPeriod--;
        float VNow;
        switch(Stage) {
            case stgPause: // Pause ended, start stage1
                if(Tick >= TickEnd) {
                    Stage = stg1;
                    Tick = 0;
                }
                break;

            case stg1:
                VNow = a1 * Tick + b1;
                if(VNow < VMax) {
                    ICurrentValue = VNow;
                    SetCurrent();
                }
                else {
                    Stage = stg2; // VMax reached
                    Tick = 0;
                }
                break;

            case stg2:
                VNow = a2 * Tick + b2;
                if(ICurrentValue > VEnd) {
                    ICurrentValue = VNow;
                    SetCurrent();
                }
                else { // end of Stage2, construct new profile
                    LedsMsgQ.SendNowOrExit(EvtMsg_t(LEDS_CONSTRUCT_PROFILE_CMD, (void*)this));
                }
                break;
        } // switch
    }
};

std::vector<BigLed_t> Leds = {
        {LED1_PIN, LED_FREQ_HZ},
        {LED2_PIN, LED_FREQ_HZ},
        {LED3_PIN, LED_FREQ_HZ},
        {LED4_PIN, LED_FREQ_HZ},
        {LED5_PIN, LED_FREQ_HZ},
};
#endif

static THD_WORKING_AREA(waLedsThread, 256);
__noreturn
static void LedsThread(void *arg) {
    chRegSetThreadName("PinSensors");
    EvtMsg_t msg;
    while(true) {
        chThdSleepMilliseconds(1);
        msg.ID = 0; // Nothing
        EvtMsg_t msg = LedsMsgQ.Fetch(TIME_IMMEDIATE);
        switch(msg.ID) {
            case LEDS_SET_BRT_CMD:
                for(BigLed_t &Led : Leds) Led.SetBrightness(msg.Value);
                break;

            case LEDS_CONSTRUCT_PROFILE_CMD: {
                BigLed_t* PLed = (BigLed_t*)msg.Ptr;
                PLed->ConstructAndStartProfile();
            } break;

            default: break;
        } // switch
        for(uint32_t i=0; i<Leds.size(); i++) {
            Leds[i].OnTick();
            TPeriodLeft[i] = Leds[i].TPeriod; // Save time left from period
        }
    } // while true
}

void LedsInit() {
    LedsMsgQ.Init();
    for(BigLed_t &Led : Leds) {
        Led.Init();
        Led.ConstructAndStartFirstProfile();
    }
    // Create and start thread
    chThdCreateStatic(waLedsThread, sizeof(waLedsThread), NORMALPRIO, (tfunc_t)LedsThread, NULL);
}

void LedsSetBrt(int32_t Brt) {
    LedsMsgQ.SendNowOrExit(EvtMsg_t(LEDS_SET_BRT_CMD, Brt));
}

void LedsSet(uint32_t Indx, uint32_t Value) {
    Leds[Indx].Set(Value);
}
