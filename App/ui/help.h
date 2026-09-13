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

#ifndef UI_HELP_H
#define UI_HELP_H

#ifdef ENABLE_QRCK_MENU_HELP

#include <stdint.h>

// Reference page for the menu's abbreviated names. Reached from the Help entry
// at the end of the menu, and scrolled one entry at a time with UP/DOWN: the
// normal submenu machinery drives it, with gSubMenuSelection as the position.

// Number of entries the page can show, for MENU_GetLimits.
uint8_t UI_HELP_Count(void);

// Draw the page for entry `index` and blit it. Takes the whole screen.
void    UI_HELP_Draw(uint8_t index);

#endif

#endif
