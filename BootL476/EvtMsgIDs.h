/*
 * EvtMsgIDs.h
 *
 *  Created on: 21 апр. 2017 г.
 *      Author: Kreyl
 */

#pragma once

enum EvtMsgId_t {
    evtIdNone = 0, // Always

    // Pretending to eternity
    evtIdShellCmd = 1,
    evtIdEverySecond = 2,

    // Audio
    evtIdSoundPlayStop = 20,
    evtIdSoundFileEnd = 21,

    // Usb
    evtIdUsbReady = 30,
    evtIdUsbNewCmd = 31,
    evtIdUsbInDone = 32,
    evtIdUsbOutDone = 33,

    // Misc periph
    evtIdButtons = 50,
    evtIdAcc = 51,

    // App specific
    evtIdClash = 120,
    evtIdSwing = 121,

};
