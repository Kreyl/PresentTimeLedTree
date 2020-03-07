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

#define LED_FREQ_HZ     630
#define LED_MAX_VALUE   255

#if 1 // ==== Leds Q ====
EvtMsgQ_t<EvtMsg_t, 18> LedsMsgQ;

#define LEDS_SET_BRT_CMD            4
#define LEDS_CONSTRUCT_PROFILE_CMD  7
#endif

#if 1 // ============ LED class =============
enum Stage_t {stg1, stg2, stg3, stg4};

void LedsTmrHandler(void *p);

/* === Profile ===
                _____ V3
               /     \
         t1   /  t3   \
    V1 ______/         \ V4
             [t2]   [t4]
*/

class BigLed_t {
private:
    const PinOutputPWM_t IChnl;
    uint32_t ICurrentValue;
    const uint32_t PWMFreq;
    uint32_t CurrBrt = LED_SMOOTH_MAX_BRT;
    void SetCurrent() {
        // CurrBrt=[0;LED_SMOOTH_MAX_BRT]; ICurrentValue=[0;255]
        IChnl.Set(ICurrentValue * CurrBrt);
    }
    void SetupDelay(uint32_t Delay_ms)  { chVTSet (&ITmr, TIME_MS2I(Delay_ms), LedsTmrHandler, this); }
    void SetupDelayI(uint32_t Delay_ms) { chVTSetI(&ITmr, TIME_MS2I(Delay_ms), LedsTmrHandler, this); }
    // Profile
    Stage_t Stage;
    uint32_t t1, Value1=0;
    uint32_t t2[LED_MAX_VALUE+1];
    uint32_t t3, Value3=LED_MAX_VALUE;
    uint32_t t4[LED_MAX_VALUE+1], Value4;
    virtual_timer_t ITmr;
    void IConstructProfValues() {
        uint32_t HalfValue = Settings.MaxValue - Settings.MinValue;
        Value1 = Value4; // Start from what we stopped at
        Value3 = Random::Generate(HalfValue, Settings.MaxValue);
        Value4 = Random::Generate(Settings.MinValue, HalfValue);
    }

public:
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
    void Set(uint32_t AValue) {
        ICurrentValue = AValue;
        SetCurrent();
    }

    void ConstructProfile() {
        IConstructProfValues();


    }

    void ConstructFirstProfile() {
        IConstructProfValues(); // As always
        Value1 = 0; // Off initially
        // Time
        t1 = Random::Generate(0, Settings.TurnOnMaxPause);
    }

    void StartProfile() {
        Stage = stg1;
        Set(Value1);
        SetupDelay(t1);
    }

    void OnTickI() {
        switch(Stage) {
            case stg1: // Stage1 ended, start next
            case stg2: // Stage2 is now
                if(ICurrentValue < LED_SMOOTH_MAX_BRT) ICurrentValue++;
                if(ICurrentValue < Value3) {
                    Stage = stg2;
                    SetupDelayI(t2[ICurrentValue]);
                }
                else { // Already at Stage3
                    Stage = stg3;
                    SetupDelayI(t3);
                }
                break;

            case stg3: // Stage3 ended, start next
            case stg4: // Stage4 is now
                if(ICurrentValue > 0) ICurrentValue--;
                if(ICurrentValue > Value4) {
                    Stage = stg4;
                    SetupDelayI(t4[ICurrentValue]);
                }
                else { // Already at end of Stage4, construct new profile
                    LedsMsgQ.SendNowOrExitI(EvtMsg_t(LEDS_CONSTRUCT_PROFILE_CMD, (void*)this));
                }
                break;
        } // switch
        SetCurrent();
    }
};

void LedsTmrHandler(void *p) {
    chSysLockFromISR();
    ((BigLed_t*)p)->OnTickI();
    chSysUnlockFromISR();
}

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
        EvtMsg_t msg = LedsMsgQ.Fetch(TIME_INFINITE);
        switch(msg.ID) {
            case LEDS_SET_BRT_CMD:
                for(BigLed_t &Led : Leds) Led.SetBrightness(msg.Value);
                break;

            case LEDS_CONSTRUCT_PROFILE_CMD: {
                BigLed_t* PLed = (BigLed_t*)msg.Ptr;
                PLed->ConstructProfile();
                PLed->StartProfile();
            } break;

            default: break;
        } // switch
    } // while true
}

void LedsInit() {
    for(BigLed_t &Led : Leds) {
        Led.Init();
        Led.ConstructFirstProfile();
        Led.StartProfile();
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
