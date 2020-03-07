/*
 * Settings.h
 *
 *  Created on: 7 мар. 2020 г.
 *      Author: layst
 */

#pragma once

class Settings_t {
private:

public:
    uint32_t TurnOnMaxPause = 900;
    uint32_t MinValue = 0;
    uint32_t MaxValue = 255;
    uint8_t Load();
};

extern Settings_t Settings;
