/*
 * Settings.cpp
 *
 *  Created on: 7 мар. 2020 г.
 *      Author: layst7
 */

#include "Settings.h"
#include "kl_lib.h"
#include "kl_fs_utils.h"

#define SETTINGS_FNAME      "config.ini"

Settings_t Settings;


uint8_t Settings_t::Load() {
    int32_t v1, v2;
    uint8_t Rslt = retvOk;
    ini::Read<int32_t>(SETTINGS_FNAME, "Common", "MinValue", &v1);
    if(v1 >= 0 and v1 <= 100) MinValue = v1;
    else Rslt = retvBadValue;
    ini::Read<int32_t>(SETTINGS_FNAME, "Common", "MinPeriod", &v1);
    ini::Read<int32_t>(SETTINGS_FNAME, "Common", "MaxPeriod", &v2);
    if((v1 >= 500 and v1 <= 55000) and (v2 >= 0 and v2 <= 60000) and (v1 <= v2)) {
        MinPeriod = v1;
        MaxPeriod = v2;
    }
    else Rslt = retvBadValue;
    return Rslt;
}
