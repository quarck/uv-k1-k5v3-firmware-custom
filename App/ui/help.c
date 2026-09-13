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

#include "help.h"

#ifdef ENABLE_QRCK_MENU_HELP

#include <string.h>

#include "../driver/st7565.h"
#include "../external/printf/printf.h"
#include "../misc.h"
#include "helper.h"
#include "menu.h"

// ---------------------------------------------------------------------------
//  Text
//
//  Keyed by menu id rather than by position, so this table cannot drift out of
//  step with MenuList when an entry is added, moved or compiled out. An id with
//  no entry here simply has no description; it never picks up the wrong one.
//
//  Entries are guarded only where the whole feature can be compiled out, and
//  purely to save flash - a missing guard costs space, never correctness.
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t     id;
    const char *text;
} help_entry_t;

static const help_entry_t HelpText[] = {
    { MENU_SQL,        "Squelch. 0 opens the audio always, 9 needs the strongest signal." },
    { MENU_STEP,       "Tuning step size used by the dial and by scanning." },
    { MENU_TXP,        "Transmit power for this channel: Low1-5, Mid, High or User." },
    { MENU_R_DCS,      "Receive DCS code. Audio stays muted unless this digital code matches." },
    { MENU_R_CTCS,     "Receive CTCSS tone. Audio stays muted unless this sub-tone matches." },
    { MENU_T_DCS,      "DCS code sent while transmitting." },
    { MENU_T_CTCS,     "CTCSS sub-tone sent while transmitting. Common for repeater access." },
    { MENU_SFT_D,      "Repeater shift direction: transmit above (+), below (-) or off." },
    { MENU_OFFSET,     "Repeater shift size: how far the transmit frequency moves." },
    { MENU_TOT,        "Transmit time-out. Keys down are cut off after this long." },
    { MENU_W_N,        "Channel bandwidth: Wide (25k) or Narrow (12.5k)." },
#ifndef ENABLE_FEAT_F4HWN
    { MENU_SCR,        "Scrambler. Inverts the audio so it is unintelligible without the same setting." },
#endif
    { MENU_BCL,        "Busy channel lockout. Refuses to transmit while the channel is in use." },
#ifdef ENABLE_FEAT_F4HWN
    { MENU_TX_LOCK,    "Per-channel transmit lock. Off lets this channel ignore the global F Lock." },
#endif
    { MENU_MEM_CH,     "Save the current VFO settings into a memory channel." },
    { MENU_DEL_CH,     "Erase a memory channel." },
    { MENU_MEM_NAME,   "Name a memory channel." },
    { MENU_LIST_CH,    "Which scan list a memory channel belongs to." },
    { MENU_S_LIST,     "Scan list used when scanning memory channels." },
    { MENU_S_PRI,      "Priority scan. Revisits the priority channels more often." },
    { MENU_S_PRI_CH_1, "First priority channel for priority scan." },
    { MENU_S_PRI_CH_2, "Second priority channel for priority scan." },
    { MENU_SC_REV,     "Scan resume: on Timeout, on Carrier drop, or Search and stop." },
    #ifdef ENABLE_QRCK_CW
    { MENU_AM,         "Demodulation for this channel. CW uses the SSB path, CWF the FM one." },
#else
    { MENU_AM,         "Demodulation mode for this channel: FM, AM or USB." },
#endif
    { MENU_COMPAND,    "Compander. Companded audio is louder and quieter on noise, both ends must match." },
#ifdef ENABLE_NOAA
    { MENU_NOAA_S,     "NOAA weather channel scan." },
#endif
    { MENU_MDF,        "What the channel line shows: frequency, name, or both." },
    { MENU_PONMSG,     "What is shown at switch-on: full screen, message, voltage or nothing." },
    { MENU_ABR,        "Backlight on-time after the last key press." },
    { MENU_ABR_ON_TX_RX, "Whether the backlight also comes on for transmit, receive, or both." },
    { MENU_ABR_MIN,    "Backlight brightness at its dimmest." },
    { MENU_ABR_MAX,    "Backlight brightness when fully on." },
    { MENU_BEEP,       "Key-press beep." },
#ifdef ENABLE_VOICE
    { MENU_VOICE,      "Spoken prompts." },
#endif
    { MENU_SAVE,       "Battery save. Longer settings idle the receiver more between checks." },
    { MENU_TDR,        "Dual watch. Listens on both VFOs, alternating between them." },
    { MENU_MIC,        "Microphone gain for transmit." },
    { MENU_MIC_BAR,    "Show a microphone level bar while transmitting." },
    { MENU_ROGER,      "Roger beep sent at the end of each transmission." },
    { MENU_STE,        "Squelch Tail Eliminate: mutes the noise burst when the other station unkeys." },
    { MENU_RP_STE,     "Repeater Squelch Tail Eliminate. The same, for the tail a repeater sends." },
    { MENU_1_CALL,     "Channel recalled by the 1-Call shortcut." },
    { MENU_AUTOLK,     "Automatic keypad lock after a period with no key presses." },
    { MENU_F1SHRT,     "Action for a short press of side key 1, the upper one." },
    { MENU_F1LONG,     "Action for a long press of side key 1, the upper one." },
    { MENU_F2SHRT,     "Action for a short press of side key 2, the lower one." },
    { MENU_F2LONG,     "Action for a long press of side key 2, the lower one." },
    { MENU_MLONG,      "Action for a long press of the MENU key." },
#ifdef ENABLE_VOX
    { MENU_VOX,        "Voice-operated transmit. Higher numbers need a louder voice to key up." },
#endif
    { MENU_BAT_TXT,    "Extra battery readout in the status bar: voltage or percentage." },
    // These five are unconditional in MenuList, so their help must be too -
    // guarding them behind ENABLE_DTMF_CALLING left them undescribed in every
    // build with DTMF calling off. Keep this block in step with MenuList.
    { MENU_UPCODE,     "DTMF sent at the start of a transmission." },
    { MENU_DWCODE,     "DTMF sent at the end of a transmission." },
    { MENU_PTT_ID,     "When the DTMF identifier is sent: at the start, the end, or both." },
    { MENU_D_ST,       "Hear DTMF tones locally as they are sent." },
    { MENU_D_PRE,      "Delay before DTMF digits are sent, to let a repeater open first." },
#ifdef ENABLE_DTMF_CALLING
    { MENU_ANI_ID,     "Your own DTMF identifier, sent as the calling station." },
    { MENU_D_RSP,      "How the radio answers a DTMF call: ring, reply, both or nothing." },
    { MENU_D_HOLD,     "How long a DTMF call stays answered before resetting." },
    { MENU_D_DCD,      "Decode DTMF received on this channel." },
    { MENU_D_LIST,     "The list of known DTMF contacts." },
#endif
    { MENU_D_LIVE_DEC, "Show DTMF digits on screen as they are received." },
#ifdef ENABLE_FEAT_F4HWN
    { MENU_VOL,        "System information: firmware identity, build, and battery." },
    { MENU_SET_PWR,    "Output power used by the User power level." },
    { MENU_SET_PTT,    "PTT style: Classic holds to talk, OnePush keys and unkeys with a tap." },
    { MENU_SET_TOT,    "Alert as the transmit time-out approaches: off, sound, light or both." },
    { MENU_SET_EOT,    "Alert at the end of a transmission: off, sound, light or both." },
    { MENU_SET_LCK,    "What the keypad lock covers: the keys alone, or the keys and PTT." },
    { MENU_SET_MET,    "S-meter style: Tiny or Classic." },
    { MENU_SET_GUI,    "How much detail the VFO lines show." },
    { MENU_SET_NAV,    "Which keys move through lists, to suit the radio's own layout." },
#endif
#ifdef ENABLE_FEAT_F4HWN_CTR
    { MENU_SET_CTR,    "Display contrast." },
#endif
#ifdef ENABLE_FEAT_F4HWN_INV
    { MENU_SET_INV,    "Invert the display: dark text on a light background." },
#endif
#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
    { MENU_SET_TMR,    "Show elapsed receive and transmit timers." },
#endif
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    { MENU_SET_OFF,    "Switch off automatically after this many minutes of no use." },
#endif
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    { MENU_SET_NFM,    "Narrow-FM bandwidth: the standard 12.5k, or a narrower 6.25k." },
#endif
#ifdef ENABLE_FEAT_F4HWN_VOL
    { MENU_SET_VOL,    "Receive audio volume." },
#endif
#ifdef ENABLE_FEAT_F4HWN_AUDIO
    { MENU_SET_AUD,    "Receive audio profile, tailoring the tone for FM or AM." },
#endif
#ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    { MENU_SET_KEY,    "Which keys stay live while the keypad is locked." },
#endif
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
    { MENU_SET_SCN,    "Scan mode, trading speed against how easily a signal is caught." },
#endif
#ifdef ENABLE_FEAT_F4HWN_LOGO_SAV
    { MENU_SET_SAV,    "Screen saver shown when the radio has been idle." },
#endif
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    { MENU_SET_CFG,    "Load a different settings bank. The radio restarts." },
#endif
#ifdef ENABLE_QRCK_CW
    { MENU_CWPITCH,    "CW beat note: the receiver is offset this far, so Morse is heard as a tone." },
#endif
#ifdef ENABLE_QRCK_CW_DECODER
    { MENU_CWSPEED,    "Speed the CW decoder starts from, in WPM. It then follows the sender." },
#endif
    { MENU_RESET,      "Factory reset. Erases settings, and optionally the channels too." },
    { MENU_F_LOCK,     "Which bands may be transmitted on. Unlock All must be confirmed several times." },
#ifndef ENABLE_FEAT_F4HWN
    { MENU_200TX,      "Allow transmit on 200MHz." },
    { MENU_350TX,      "Allow transmit on 350MHz." },
    { MENU_500TX,      "Allow transmit on 500MHz." },
#endif
    { MENU_350EN,      "Allow the 350MHz band to be received." },
#ifndef ENABLE_FEAT_F4HWN
    { MENU_SCREN,      "Enable the scrambler menu." },
#endif
#ifdef ENABLE_F_CAL_MENU
    { MENU_F_CALI,     "Reference crystal calibration. Changing this detunes the whole radio." },
#endif
    { MENU_BATCAL,     "Battery voltage calibration." },
    { MENU_BATTYP,     "Battery capacity fitted, so the gauge reads correctly." },
    { MENU_HELP,       "This page. UP and DOWN move through the entries, EXIT leaves." },
};

// ---------------------------------------------------------------------------
//  Lookup and layout
// ---------------------------------------------------------------------------

#define HELP_COLS       18u     // characters per line in the small font
#define HELP_LINES      5u      // text lines below the heading

static const char *HELP_Lookup(uint8_t id)
{
    for (uint8_t i = 0; i < ARRAY_SIZE(HelpText); i++)
        if (HelpText[i].id == id)
            return HelpText[i].text;

    return NULL;
}

uint8_t UI_HELP_Count(void)
{
    uint8_t n = 0;

    while (MenuList[n].name[0] != '\0')
        n++;

    return n;
}

// Copy one line's worth of `text` into `out`, breaking at the last space that
// fits so words stay whole. Returns where the next line starts.
static const char *HELP_WrapLine(const char *text, char *out)
{
    uint8_t len = 0;
    uint8_t brk = 0;      // last space that would fit

    while (text[len] != '\0' && len < HELP_COLS) {
        if (text[len] == ' ')
            brk = len;
        len++;
    }

    if (text[len] != '\0' && text[len] != ' ' && brk > 0)
        len = brk;        // mid-word: back up to the space

    memcpy(out, text, len);
    out[len] = '\0';

    text += len;
    while (*text == ' ')  // swallow the break
        text++;

    return text;
}

void UI_HELP_Draw(uint8_t index)
{
    char line[HELP_COLS + 1];
    char heading[HELP_COLS + 1];

    const uint8_t count = UI_HELP_Count();
    if (index >= count)
        index = count ? (uint8_t)(count - 1u) : 0u;

    UI_DisplayClear();

    // Heading: the menu name exactly as it appears in the list, plus how far
    // through the entries we are.
    sprintf(heading, "%-6s     %2u/%2u", MenuList[index].name,
            (unsigned)(index + 1u), (unsigned)count);
    UI_PrintStringSmallBold(heading, 0, 0, 0);

    const char *text = HELP_Lookup(MenuList[index].menu_id);
    if (text == NULL)
        text = "No description.";

    for (uint8_t row = 0; row < HELP_LINES && *text != '\0'; row++) {
        text = HELP_WrapLine(text, line);
        UI_PrintStringSmallNormal(line, 0, 0, (uint8_t)(row + 2u));
    }

    ST7565_BlitFullScreen();
}

#endif
