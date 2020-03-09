/*
 * Sequences.h
 *
 *  Created on: 09 џэт. 2015 у.
 *      Author: Kreyl
 */

#pragma once

#include "ChunkTypes.h"

#if 0 // ============================ LED RGB ==================================
const LedRGBChunk_t lsqStart[] = {
        {csSetup, 90, clRed},
        {csSetup, 90, clGreen},
        {csSetup, 90, clBlue},
        {csSetup, 90, {0, 2, 0}},
        {csEnd}
};

const LedRGBChunk_t lsqFailure[] = {
        {csSetup, 0, clRed},
        {csWait, 90},
        {csSetup, 0, clRed},
        {csWait, 90},
        {csSetup, 0, clRed},
        {csWait, 90},
        {csSetup, 0, clRed},
        {csWait, 90},
        {csSetup, 0, clRed},
        {csWait, 90},
        {csSetup, 0, clRed},
        {csWait, 90},
        {csEnd}
};

const LedRGBChunk_t lsqCharging[] = {
        {csSetup, 360, clBlue},
        {csSetup, 360, clBlack},
        {csGoto, 0}
};

const LedRGBChunk_t lsqOperational[] = {
        {csSetup, 90, {0, 2, 0}},
        {csEnd}
};

//const LedRGBChunk_t lsqHit[] = {
//        {csSetup, 0, clYellow},
//        {csWait, 99},
//        {csSetup, 0, clBlack},
//        {csWait, 360},
//        {csEnd}
//};
//
//const LedRGBChunk_t lsqDamaged[] = {
//        {csSetup, 0, clYellow},
//        {csEnd}
//};
//
//const LedRGBChunk_t lsqReload[] = {
//        {csSetup, 0, clGreen},
//        {csWait, 99},
//        {csSetup, 0, {0,90,0}},
//        {csWait, 99},
//        {csGoto, 0}
//};
//
//const LedRGBChunk_t lsqDestroyed[] = {
//        {csSetup, 0, clRed},
//        {csEnd}
//};


//const LedRGBChunk_t lsqCharging[] = {
//        {csSetup, 540, {0,9,0}},
//        {csSetup, 540, clBlack},
//        {csWait, 900},
//        {csGoto, 0}
//};
//
//const LedRGBChunk_t lsqChargingDone[] = {
//        {csSetup, 0, {0,9,0}},
//        {csEnd}
//};
//
//const LedRGBChunk_t lsqDischarged[] = {
//        {csSetup, 0, clRed},
//        {csWait, 180},
//        {csSetup, 0, clBlack},
//        {csWait, 360},
//        {csGoto, 0}
//};
#endif

#if 1 // Blinker LED
const BaseChunk_t lsqWriting[] = {
        {csSetup, 1},
        {csWait, 207},
        {csSetup, 0},
        {csWait, 45},
        {csGoto, 0}
};

const BaseChunk_t lsqError[] = {
        {csSetup, 1},
        {csWait, 72},
        {csSetup, 0},
        {csWait, 72},
        {csGoto, 0}
};

#endif
