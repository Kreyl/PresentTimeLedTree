/*
 * Settings.h
 *
 *  Created on: 7 мар. 2020 г.
 *      Author: layst
 */

#pragma once

#include <inttypes.h>

class Settings_t {
private:

public:
    int32_t TurnOnMaxPause = 900;
    int32_t MinValue = 0;
    int32_t MaxValue = 255;
    int32_t MinPeriod = 2700, MaxPeriod = 5400;
    uint8_t Load();
};

extern Settings_t Settings;
