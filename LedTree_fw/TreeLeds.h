/*
 * TreeLeds.h
 *
 *  Created on: 5 ���. 2020 �.
 *      Author: layst
 */

#pragma once

#include <inttypes.h>
#include "MsgQ.h"

void LedsInit();
void LedsSetBrt(int32_t Brt);
void LedsSet(uint32_t Indx, uint32_t Value);
