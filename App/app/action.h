/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifndef APP_ACTION_H
#define APP_ACTION_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"

void ACTION_Power(void);
void ACTION_Monitor(void);
void ACTION_Scan(bool bRestart);
#ifdef ENABLE_VOX
    void ACTION_Vox(void);
#endif

#ifdef ENABLE_FMRADIO
    void ACTION_FM(void);
#endif
void ACTION_SwitchDemodul(void);

#ifdef ENABLE_FEAT_F4HWN
    void ACTION_RxMode(void);
    void ACTION_MainOnly(void);
    void ACTION_Ptt(void);
    void ACTION_Wn(void);
    void ACTION_BackLightOnDemand(void);
    void ACTION_BackLight(void);
    void ACTION_Mute(void);
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        void ACTION_RxA(void);
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        void ACTION_Power_High(void);
        void ACTION_Remove_Offset(void);
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
        void ACTION_RxTxLog(void);
    #endif
#endif

#ifdef ENABLE_QRCK_SQL_ADJUST
// Squelch-adjust overlay: a side function opens it, UP/DOWN then step the
// squelch one point at a time until another key or the timeout closes it.
// Unlike F + UP/DOWN (see processFKeyFunction), which only changes the level
// for the current session, this one edits the stored setting.
#define SQL_ADJUST_TIMEOUT_500MS 6u   // 3 s of no keys closes the overlay
extern bool    gSqlAdjustMode;
extern uint8_t gSqlAdjustTimeout_500ms;
void ACTION_SqlAdjust(void);
void SQL_ADJUST_Exit(void);
// true when the key belongs to the overlay and must not be handled elsewhere
bool SQL_ADJUST_ProcessKey(KEY_Code_t key, bool isPressed, bool isHeld);
#endif

#ifdef ENABLE_FEAT_F4HWN_ACTION_PICKER
#define ACTION_PICKER_TIMEOUT_500MS 10u
extern uint8_t gActionPickerKey;
extern uint8_t gActionPickerSelection[2];
extern uint8_t gActionPickerTimeout_500ms;
bool ACTION_PickerProcessKey(KEY_Code_t key, bool isPressed, bool isHeld);
#endif
bool ACTION_IsAvailable(uint8_t action);
void ACTION_Handle(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

#endif
