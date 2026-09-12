/* Copyright 2026 quarck
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

#ifndef APP_CW_H
#define APP_CW_H

#ifdef ENABLE_QRCK_CW_DECODER

// CW decoder: reads Morse off the RSSI envelope. Keying is on/off, so the
// envelope carries the message and no audio path to the MCU is needed. Takes
// over the CPU like the spectrum analyser, which is what lets it poll RSSI
// every few milliseconds instead of being limited to the 10 ms SysTick.
void APP_RunCw(void);

#endif

#endif
