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

#include "app/cw.h"

#ifdef ENABLE_QRCK_CW_DECODER

#include <string.h>

#include "audio.h"
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/ui.h"
#if defined(ENABLE_UART) || defined(ENABLE_USB)
    #include "app/uart.h"
#endif

// ---------------------------------------------------------------------------
//  Morse code table
//
//  Same sentinel-prefixed encoding the beacon uses (see FOXHUNT_MORSE_LETTER):
//  after the leading 1 sentinel bit, each lower bit is one element read
//  MSB-first, 0 = dit and 1 = dah. The form is reversible, so one table serves
//  both directions: encoding walks down from the sentinel, decoding shifts a
//  code up from 1 and looks the result up.
// ---------------------------------------------------------------------------

static const uint8_t CW_LETTER[26] = {
    0x05, 0x18, 0x1A, 0x0C, 0x02, 0x12, 0x0E, 0x10, 0x04, 0x17,   // A..J
    0x0D, 0x14, 0x07, 0x06, 0x0F, 0x16, 0x1D, 0x0A, 0x08, 0x03,   // K..T
    0x09, 0x11, 0x0B, 0x19, 0x1B, 0x1C,                           // U..Z
};
static const uint8_t CW_DIGIT[10] = {
    0x3F, 0x2F, 0x27, 0x23, 0x21, 0x20, 0x30, 0x38, 0x3C, 0x3E,   // 0..9
};

// The punctuation worth having in a QSO. Six elements each, so the sentinel
// sits in bit 6 and the code still fits a uint8_t.
static const struct { uint8_t code; char ch; } CW_PUNCT[] = {
    { 0x32, '/' }, { 0x55, '.' }, { 0x73, ',' },
    { 0x4C, '?' }, { 0x31, '=' }, { 0x61, '-' }, { 0x2A, '+' },
};

// Widest code is 7 elements (sentinel in bit 7); beyond that the run is noise.
#define CW_ELEM_MAX         7u

// Reverse lookup: sentinel-prefixed code -> character, 0 when unknown.
static char CW_CodeToChar(uint8_t code)
{
    if (code <= 1)
        return 0;

    for (uint8_t i = 0; i < 26; i++)
        if (CW_LETTER[i] == code)
            return (char)('A' + i);

    for (uint8_t i = 0; i < 10; i++)
        if (CW_DIGIT[i] == code)
            return (char)('0' + i);

    for (uint8_t i = 0; i < ARRAY_SIZE(CW_PUNCT); i++)
        if (CW_PUNCT[i].code == code)
            return CW_PUNCT[i].ch;

    return 0;
}

// ---------------------------------------------------------------------------
//  Tuning constants
// ---------------------------------------------------------------------------

#define CW_SAMPLE_MS        2u      // RSSI poll cadence
#define CW_TONE_HZ          700u    // sidetone / keyed tone

// All receive timing is measured in samples, never milliseconds, and every
// threshold is derived from the measured dit length. A constant error in the
// real sample period therefore cancels out of the decoder entirely — it only
// shifts the WPM shown on screen, which is why the loop can get away with a
// plain delay instead of a hardware timer.
#define CW_DIT_MIN          6u      // ~100 WPM ceiling
#define CW_DIT_MAX          150u    // ~4 WPM floor
#define CW_DIT_DEFAULT      40u     // ~15 WPM at CW_SAMPLE_MS, the fallback prior

// The dit length is tracked as the shortest run seen recently, not as a running
// mean of elements already classified: a mean seeded too high reads every dah
// as a dit and then drifts further the wrong way, which loses fast senders
// completely. Taking the minimum is self-correcting instead — one real dit
// pulls the estimate straight down. Intra-character gaps count too, since those
// are exactly one unit by definition and arrive earlier than a lone dit would.
// A bare minimum is still too fragile in both directions: one short noise blip
// collapses it, and once it sits too low every element reads as a dah, so no
// further evidence can ever lift it again. Two things prevent that deadlock -
// candidates are combined as a median of the last three, which ignores a single
// outlier, and the estimate creeps upward on a timer, so a sender who slows
// down (or a collapse caused by noise) is recovered from even while nothing is
// being classified correctly.
#define CW_CAND_N           3u
#define CW_CREEP_DIV        32u

// An estimate that is far too low has a distinctive signature: the one-unit
// gaps inside a character are mistaken for character gaps, so every element is
// emitted as its own single-element character. Real text does produce the odd
// lone E or T, but not a run of them, so a run that long is taken as evidence
// the estimate needs lifting and not as text.
#define CW_SINGLES_MAX      4u

// Consecutive samples past a threshold before the key state flips. Noise
// crossings last a sample or two; the shortest element we care about is ~15
// samples, so a short debounce discriminates cleanly and lets CW_MIN_SWING stay
// low enough for weak signals.
#define CW_DEBOUNCE         3u

// A run longer than this is a stuck carrier, not an element: drop it so a held
// signal cannot poison the speed estimate.
#define CW_ON_MAX           (CW_DIT_MAX * 4u)

// Envelope swing (in RSSI counts) below which the channel is treated as idle.
// Without it the hysteresis band collapses onto the noise and the decoder
// happily prints garbage from the noise floor.
#define CW_MIN_SWING        24u

// Input smoothing. Deliberately light: RSSI is only 9 bits and a long time
// constant eats into short elements (a 40 WPM dit is ~15 samples).
#define CW_ENV_SHIFT        1u

// Floor/peak trackers move by one count every CW_TRACK_DIV samples, which is
// ~60 counts/s at CW_SAMPLE_MS. A shift-based decay cannot be used here: the
// differences involved are almost always under 512, so any >> 9 truncates to
// zero and the tracker freezes for good.
#define CW_TRACK_DIV        8u

// Send speed is held in WPM, the unit the operator thinks in, and converted to
// samples only when keying. Stepping the sample count instead makes the WPM
// steps wildly uneven - 12 WPM per press at the fast end, a tenth of one at the
// slow end - because the two are reciprocal.
#define CW_WPM_MIN          5u
#define CW_WPM_MAX          40u
#define CW_WPM_UNITS(wpm)   ((uint16_t)(1200u / ((wpm) * CW_SAMPLE_MS)))

#define CW_RX_COLS          18u     // characters per display line
#define CW_RX_ROWS          6u      // decoded-text lines, the whole screen but one

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------

static bool     cwRunning;
static uint8_t  cwWpmHint;          // expected sender speed, WPM (see cwDitSeed)
static uint8_t  cwWpmHintSaved;     // value on entry, to know whether to persist

// receive
static uint16_t cwEnv;              // smoothed RSSI
static uint16_t cwEnvMin, cwEnvMax; // tracked noise floor and peak
static uint8_t  cwTrackTick;        // divider for the tracker's one-count steps
static bool     cwKeyDown;
static uint16_t cwOnCount, cwOffCount;
static uint16_t cwDitEst;           // adaptive dit length, in samples
static uint16_t cwDitSeed = CW_DIT_DEFAULT;   // prior the estimate starts from
static uint16_t cwCand[CW_CAND_N];  // recent one-unit observations
static uint8_t  cwCandIdx;
static uint16_t cwCreepTick;        // divider for the estimate's upward creep
static uint8_t  cwEdge;             // consecutive samples past the threshold
static uint8_t  cwSingles;          // consecutive single-element characters
static bool     cwCharDone;         // character gap already acted on
static bool     cwWordDone;         // word gap already acted on
static uint16_t cwRun[CW_ELEM_MAX]; // element lengths of the character in progress
static uint8_t  cwElems;            // elements buffered in cwRun
static bool     cwCandPrimed;       // candidate set seeded from a real observation

// decoded text, as a 3-line scrolling terminal
static char     cwRx[CW_RX_ROWS][CW_RX_COLS + 1];
static uint8_t  cwRxCol;

static bool     cwRedraw;

// ---------------------------------------------------------------------------
//  Decoded-text terminal
// ---------------------------------------------------------------------------

static void CW_RxClear(void)
{
    memset(cwRx, 0, sizeof(cwRx));
    cwRxCol = 0;
    cwRedraw = true;
}

static void CW_RxPutChar(char c)
{
    if (cwRxCol >= CW_RX_COLS) {   // bottom line full: scroll up one line
        for (uint8_t row = 0; row + 1u < CW_RX_ROWS; row++)
            memcpy(cwRx[row], cwRx[row + 1u], CW_RX_COLS + 1);
        memset(cwRx[CW_RX_ROWS - 1u], 0, CW_RX_COLS + 1);
        cwRxCol = 0;
    }

    cwRx[CW_RX_ROWS - 1u][cwRxCol++] = c;
    cwRedraw = true;
}

// ---------------------------------------------------------------------------
//  Receive: RSSI envelope -> elements -> characters
// ---------------------------------------------------------------------------

static void CW_RxReset(void)
{
    const uint16_t rssi = BK4819_GetRSSI();

    cwEnv       = rssi;
    cwEnvMin    = rssi;
    cwEnvMax    = rssi;
    cwTrackTick = 0;

    cwKeyDown   = false;
    cwOnCount   = 0;
    cwOffCount  = 0;
    cwDitEst    = cwDitSeed;
    cwCandIdx   = 0;
    cwCreepTick = 0;
    cwEdge      = 0;
    cwSingles   = 0;
    cwCharDone  = false;
    cwWordDone  = false;
    cwCandPrimed = false;

    for (uint8_t i = 0; i < CW_CAND_N; i++)
        cwCand[i] = cwDitSeed;
    cwElems     = 0;
}

static void CW_TrackSpeed(uint16_t samples);

// Flush the character under construction, if any.
static void CW_EmitChar(void)
{
    if (cwElems == 0)
        return;

    // Shortest element of the character is a dit, unless the character is all
    // dahs -- the median absorbs that case.
    uint16_t shortest = cwRun[0];
    for (uint8_t i = 1; i < cwElems; i++)
        if (cwRun[i] < shortest)
            shortest = cwRun[i];
    if (shortest <= (uint16_t)(cwDitEst * 2u))
        CW_TrackSpeed(shortest);

    uint8_t code = 1;
    for (uint8_t i = 0; i < cwElems; i++)
        code = (uint8_t)((code << 1) |
                         (cwRun[i] > (uint16_t)(cwDitEst * 2u) ? 1u : 0u));

    const char c = CW_CodeToChar(code);
    CW_RxPutChar(c ? c : '*');   // '*' = a valid run of elements we cannot name

    // See CW_SINGLES_MAX: a run of single-element characters means the gaps are
    // being cut in the wrong places, so lift the estimate rather than keep
    // printing one letter per element.
    if (cwElems == 1) {
        if (++cwSingles >= CW_SINGLES_MAX) {
            cwSingles = 0;
            uint16_t est = (uint16_t)((cwDitEst * 3u) / 2u);
            if (est <= cwDitEst) est = (uint16_t)(cwDitEst + 1u);
            if (est > CW_DIT_MAX)  est = CW_DIT_MAX;
            cwDitEst = est;
        }
    }
    else {
        cwSingles = 0;
    }

    cwElems = 0;
}

// Fold a run known to be one unit long into the speed estimate: instantly
// downward, slowly upward. Called with elements short enough to be dits and
// with intra-character gaps, which are one unit by definition.
static void CW_TrackSpeed(uint16_t samples)
{
    if (samples < CW_DIT_MIN) samples = CW_DIT_MIN;
    if (samples > CW_DIT_MAX) samples = CW_DIT_MAX;

    if (!cwCandPrimed) {
        // Seed every slot from the first real observation, or the median would
        // keep returning the placeholder default until three have arrived.
        cwCandPrimed = true;
        for (uint8_t i = 0; i < CW_CAND_N; i++)
            cwCand[i] = samples;
    }
    else {
        cwCand[cwCandIdx] = samples;
        if (++cwCandIdx >= CW_CAND_N)
            cwCandIdx = 0;
    }

    // Median of three, written out rather than sorted.
    const uint16_t a = cwCand[0], b = cwCand[1], c = cwCand[2];
    uint16_t med;
    if ((a <= b && b <= c) || (c <= b && b <= a))      med = b;
    else if ((b <= a && a <= c) || (c <= a && a <= b)) med = a;
    else                                              med = c;

    cwDitEst    = med;
    cwCreepTick = 0;
}

// One element finished. Only its length is recorded here; dit-versus-dah is
// decided in CW_EmitChar, once the character is complete. Classifying on
// arrival would judge the first element of a transmission against whatever
// estimate happened to be left over, while waiting means the one-unit gaps
// inside this very character have already refined it.
static void CW_PushElement(uint16_t samples)
{
    if (cwElems < CW_ELEM_MAX)
        cwRun[cwElems++] = samples;
    else
        cwElems = 0;   // overrun: abandon rather than mis-print
}

static void CW_RxSample(void)
{
    const uint16_t rssi = BK4819_GetRSSI();

    // Smooth the input a little: one RSSI read is noisy, and the edges matter
    // more than the absolute level.
    cwEnv += ((int32_t)rssi - (int32_t)cwEnv) >> CW_ENV_SHIFT;

    // Track the floor and the peak: instant in the direction the signal moves,
    // then creeping back one count at a time so the thresholds follow fading
    // and AGC movement without being dragged by a single element.
    if (++cwTrackTick >= CW_TRACK_DIV) {
        cwTrackTick = 0;
        if (cwEnvMax > cwEnv)
            cwEnvMax--;
        if (cwEnvMin < cwEnv)
            cwEnvMin++;
    }

    if (cwEnv > cwEnvMax)
        cwEnvMax = cwEnv;
    if (cwEnv < cwEnvMin)
        cwEnvMin = cwEnv;

    const uint16_t swing = (uint16_t)(cwEnvMax - cwEnvMin);

    if (swing < CW_MIN_SWING) {
        // Idle channel. Close out anything pending and stop looking, so the
        // noise floor is never decoded.
        if (cwKeyDown) {
            cwKeyDown = false;
            cwOnCount = 0;
        }
        CW_EmitChar();
        cwOffCount = 0;
        return;
    }

    // Recover from an estimate that has ended up too low to classify anything:
    // see CW_CREEP_DIV. Only while the key is actually down, which is the only
    // time misclassification can be happening. Creeping through the gaps as
    // well would inflate the estimate by half over a word gap and then misjudge
    // the first element after it, and creeping through a silence would leave a
    // useless prior for the next station. A correctly tracking decoder is pulled
    // straight back down by the next dit, so the drift is harmless when it is
    // not needed.
    if (cwKeyDown && ++cwCreepTick >= CW_CREEP_DIV) {
        cwCreepTick = 0;
        if (cwDitEst < CW_DIT_MAX)
            cwDitEst++;
    }

    // Hysteresis either side of the midpoint, at 5/8 and 3/8 of the swing.
    const uint16_t high = (uint16_t)(cwEnvMin + ((swing * 5u) >> 3));
    const uint16_t low  = (uint16_t)(cwEnvMin + ((swing * 3u) >> 3));

    if (cwKeyDown) {
        cwOnCount++;

        cwEdge = (cwEnv < low) ? (uint8_t)(cwEdge + 1u) : 0u;

        if (cwEdge >= CW_DEBOUNCE) {       // element ended
            cwKeyDown = false;
            cwEdge    = 0;

            // The debounce samples were already below the threshold, so they
            // belong to the gap that follows rather than to the element.
            const uint16_t len = (uint16_t)(cwOnCount - CW_DEBOUNCE);
            if (len <= CW_ON_MAX)
                CW_PushElement(len);

            cwOnCount  = 0;
            cwOffCount = CW_DEBOUNCE;
        }
        else if (cwOnCount > CW_ON_MAX) {  // stuck carrier: give up on it
            cwElems = 0;
        }
        return;
    }

    cwOffCount++;

    cwEdge = (cwEnv > high) ? (uint8_t)(cwEdge + 1u) : 0u;

    if (cwEdge >= CW_DEBOUNCE) {           // element started
        cwKeyDown = true;
        cwEdge    = 0;

        // An intra-character gap is exactly one unit, which makes it the
        // earliest reliable measure of the sender's speed.
        const uint16_t gap = (uint16_t)(cwOffCount - CW_DEBOUNCE);
        if (cwElems > 0 && gap <= (uint16_t)(cwDitEst * 2u))
            CW_TrackSpeed(gap);

        cwOnCount  = CW_DEBOUNCE;
        cwOffCount = 0;
        cwCharDone = false;
        cwWordDone = false;
        return;
    }

    // Gaps are 1 unit inside a character, 3 between characters and 7 between
    // words, so the thresholds sit at the midpoints: 2 and 5. Compared with >=
    // and latched rather than tested for equality, because cwDitEst moves
    // underneath them (see CW_CREEP_DIV) and an exact match is then never seen.
    // The 5-unit midpoint also leaves room for that drift, which a 6 does not.
    if (!cwCharDone && cwOffCount >= (uint16_t)(cwDitEst * 2u)) {
        cwCharDone = true;
        CW_EmitChar();
    }
    else if (!cwWordDone && cwOffCount >= (uint16_t)(cwDitEst * 5u)) {
        cwWordDone = true;
        if (cwRxCol > 0 && cwRx[CW_RX_ROWS - 1u][cwRxCol - 1] != ' ')
            CW_RxPutChar(' ');
    }
}

// ---------------------------------------------------------------------------
//  Display
// ---------------------------------------------------------------------------

static void CW_Draw(void)
{
    char line[24];

    UI_DisplayClear();

    // The measured speed, the hint it started from, and the live envelope swing.
    // The swing is the number to watch when nothing is being decoded: below
    // CW_MIN_SWING the channel counts as idle and nothing is read at all.
    const uint16_t swing = (uint16_t)(cwEnvMax - cwEnvMin);
    uint16_t rxWpm = (uint16_t)(1200u / (cwDitEst * CW_SAMPLE_MS));
    if (rxWpm > 99u)
        rxWpm = 99u;

    sprintf(line, "CW %2uWPM ~%2u  %3u", (unsigned)rxWpm, (unsigned)cwWpmHint,
            (unsigned)(swing > 999u ? 999u : swing));
    UI_PrintStringSmallNormal(line, 0, 0, 0);

    for (uint8_t row = 0; row < CW_RX_ROWS; row++)
        if (cwRx[row][0])
            UI_PrintStringSmallNormal(cwRx[row], 0, 0, (uint8_t)(row + 1u));

    ST7565_BlitFullScreen();
}

// ---------------------------------------------------------------------------
//  Keys
// ---------------------------------------------------------------------------

static void CW_HandleKey(KEY_Code_t key)
{
    switch (key) {
        case KEY_MENU:                                  // clear the text
        case KEY_STAR:
            CW_RxClear();
            break;

        // The speed hint only seeds the estimate; the decoder tracks the real
        // speed by itself. Nudging it helps lock onto a station far from the
        // configured value without waiting for the estimate to walk there.
        case KEY_UP:
            if (cwWpmHint < CW_WPM_MAX) {
                cwWpmHint++;
                cwDitSeed = CW_WPM_UNITS(cwWpmHint);
            }
            cwRedraw = true;
            break;

        case KEY_DOWN:
            if (cwWpmHint > CW_WPM_MIN) {
                cwWpmHint--;
                cwDitSeed = CW_WPM_UNITS(cwWpmHint);
            }
            cwRedraw = true;
            break;

        case KEY_EXIT:
            cwRunning = false;
            break;

        // PTT is deliberately inert: this screen never transmits, and leaving it
        // on a PTT press would key up the moment the user let go.
        case KEY_PTT:
        default:
            return;
    }

    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
}

// ---------------------------------------------------------------------------
//  Entry
// ---------------------------------------------------------------------------

void APP_RunCw(void)
{
    KEY_Code_t prevKey = KEY_INVALID;
    uint16_t   drawTick = 0;

    cwWpmHint = gEeprom.CW_SPEED;
    if (cwWpmHint < CW_WPM_MIN) cwWpmHint = CW_WPM_MIN;
    if (cwWpmHint > CW_WPM_MAX) cwWpmHint = CW_WPM_MAX;
    cwWpmHintSaved = cwWpmHint;

    // The estimate has to start somewhere, and a configured expectation beats a
    // fixed default: starting far from the truth splits a slow sender's
    // characters up (their real gaps look too long) and runs a fast sender's
    // together, and a short message can finish before the estimate converges.
    cwDitSeed = CW_WPM_UNITS(cwWpmHint);
    if (cwDitSeed < CW_DIT_MIN) cwDitSeed = CW_DIT_MIN;
    if (cwDitSeed > CW_DIT_MAX) cwDitSeed = CW_DIT_MAX;
    CW_RxClear();
    CW_RxReset();

    BACKLIGHT_TurnOn();

    cwRunning = true;
    cwRedraw  = true;

    while (cwRunning) {
        CW_RxSample();

        const KEY_Code_t key = KEYBOARD_GetKey();

        if (key != prevKey) {
            prevKey = key;
            if (key != KEY_INVALID)
                CW_HandleKey(key);
        }

        if (gBeepToPlay != BEEP_NONE) {
            AUDIO_PlayBeep(gBeepToPlay);
            gBeepToPlay = BEEP_NONE;
            CW_RxReset();              // the beep disturbed the envelope
        }

        // Redraw on change, and periodically for the live level and speed.
        if (++drawTick >= 100u) {
            drawTick = 0;
            cwRedraw = true;
        }

        if (cwRedraw) {
            cwRedraw = false;
            CW_Draw();
        }

        if (gNextTimeslice) {
            gNextTimeslice = false;
            BACKLIGHT_Update();
        }

#if defined(ENABLE_UART) || defined(ENABLE_USB)
        UART_ServiceCommands();
#endif

        SYSTEM_DelayMs(CW_SAMPLE_MS);
    }

    // Persist an adjustment made with UP/DOWN, once, on the way out rather than
    // on every keypress.
    if (cwWpmHint != cwWpmHintSaved) {
        gEeprom.CW_SPEED = cwWpmHint;
        SETTINGS_SaveSettings();
    }

    gRequestDisplayScreen = DISPLAY_MAIN;
    BACKLIGHT_TurnOn();
}

#endif
