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

#define LED_FREQ_HZ     450

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
        if(x != 0 and y < 1) y = 1;
        y = y  * CurrBrt;
        IChnl.Set((int32_t)y);
//        if(ICurrentValue < 19.0) Printf("x: %f; y: %f\r", x, y);
    }
    // Profile
    Stage_t Stage;
    float VMax, VEnd;
    float Tick, TickEnd;
    float a1, b1, a2, b2;
    void IConstructProfValues() {
//        uint32_t HalfValue = Settings.MinValue + (Settings.MaxValue - Settings.MinValue) / 2;
//        VMax = Random::Generate(HalfValue, Settings.MaxValue);
//        VEnd = Random::Generate(Settings.MinValue, HalfValue);
        VMax = Settings.MaxValue;
        VEnd = Settings.MinValue;
//        Printf("max=%d; end=%d\r", (int32_t)VMax, (int32_t)VEnd);
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
//            Printf("%d; ", (int32_t)TPeriodLeft[i]);
            if(TPeriodLeft[i] >= (Settings.MinPeriod / 2) and TPeriodLeft[i] <= (Settings.MaxPeriod / 2)) {
                AllAreOut = false;
                break;
            }
        }
        if(AllAreOut) {
            TPeriod = Random::Generate(Settings.MinPeriod, Settings.MaxPeriod);
//            Printf("T1: %d\r", (int32_t)TPeriod);
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
//            Printf("T2: %d\r", (int32_t)TPeriod);
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

static int32_t BackTable[256] = {0, 28, 41, 49, 56, 61, 65, 69, 73, 76, 79, 82, 85, 87, 90, 92, 94, 96, 98, 100, 102, 104, 106, 108, 109, 111, 113, 114, 116, 117, 119, 120, 121, 123, 124, 125, 127, 128, 129, 130, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 144, 145, 146, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 157, 158, 159, 160, 161, 162, 162, 163, 164, 165, 166, 166, 167, 168, 169, 169, 170, 171, 172, 172, 173, 174, 175, 175, 176, 177, 177, 178, 179, 179, 180, 181, 181, 182, 183, 183, 184, 185, 185, 186, 187, 187, 188, 188, 189, 190, 190, 191, 191, 192, 193, 193, 194, 194, 195, 196, 196, 197, 197, 198, 198, 199, 200, 200, 201, 201, 202, 202, 203, 203, 204, 204, 205, 206, 206, 207, 207, 208, 208, 209, 209, 210, 210, 211, 211, 212, 212, 213, 213, 214, 214, 215, 215, 216, 216, 217, 217, 218, 218, 219, 219, 219, 220, 220, 221, 221, 222, 222, 223, 223, 224, 224, 225, 225, 225, 226, 226, 227, 227, 228, 228, 229, 229, 229, 230, 230, 231, 231, 232, 232, 232, 233, 233, 234, 234, 235, 235, 235, 236, 236, 237, 237, 237, 238, 238, 239, 239, 239, 240, 240, 241, 241, 241, 242, 242, 243, 243, 243, 244, 244, 245, 245, 245, 246, 246, 246, 247, 247, 248, 248, 248, 249, 249, 249, 250, 250, 251, 251, 251, 252, 252, 252, 253, 253, 254, 254, 254, 255, 255, 255};

void LedsInit() {
    LedsMsgQ.Init();
    // Convert settings min value
    Settings.MinValue = BackTable[Settings.MinValue];
    Printf("Real min value: %d\r", Settings.MinValue);
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
