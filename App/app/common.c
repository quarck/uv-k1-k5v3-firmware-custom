#include "app/chFrScanner.h"
#include "app/main.h"
#include "audio.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

void COMMON_KeypadLockToggle() 
{

    if (gScreenToDisplay != DISPLAY_MENU &&
        gCurrentFunction != FUNCTION_TRANSMIT)
    {   // toggle the keyboad lock

        #ifdef ENABLE_VOICE
            gAnotherVoiceID = gEeprom.KEY_LOCK ? VOICE_ID_UNLOCK : VOICE_ID_LOCK;
        #endif

        gEeprom.KEY_LOCK = !gEeprom.KEY_LOCK;

        gRequestSaveSettings = true;
    }
}

void COMMON_SwitchVFOs()
{
#ifdef ENABLE_SCAN_RANGES    
    gScanRangeStart = 0;
#endif
    // Before TX_VFO moves: the restore puts the backup back into whichever VFO
    // is current, and the entry being abandoned belongs to the one we are leaving.
    if (gInputBoxIndex > 0) {
        MAIN_CancelFreqInput();
        gInputBoxIndex = 0;
        gHasVfoBackup = false;
    }

    gEeprom.TX_VFO ^= 1;

    if (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF)
        gEeprom.CROSS_BAND_RX_TX = gEeprom.TX_VFO + 1;
    if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF)
        gEeprom.DUAL_WATCH = gEeprom.TX_VFO + 1;

    gRequestSaveSettings  = 1;
    gFlagReconfigureVfos  = true;
    gScheduleDualWatch = true;

    gRequestDisplayScreen = DISPLAY_MAIN;
}

void COMMON_SwitchVFOMode()
{
#ifdef ENABLE_NOAA
    if (gEeprom.VFO_OPEN && !IS_NOAA_CHANNEL(gTxVfo->CHANNEL_SAVE))
#else
    if (gEeprom.VFO_OPEN)
#endif
    {
        if (gInputBoxIndex > 0) {
            MAIN_CancelFreqInput();
            gInputBoxIndex = 0;
            gHasVfoBackup = false;
        }

        if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE))
        {   // swap to frequency mode
            gEeprom.ScreenChannel[gEeprom.TX_VFO] = gEeprom.FreqChannel[gEeprom.TX_VFO];
            #ifdef ENABLE_VOICE
                gAnotherVoiceID        = VOICE_ID_FREQUENCY_MODE;
            #endif
            gRequestSaveVFO            = true;
            gVfoConfigureMode          = VFO_CONFIGURE_RELOAD;
            return;
        }

        uint16_t Channel = RADIO_FindNextChannel(gEeprom.MrChannel[gEeprom.TX_VFO], 1, false, 0);
        if (Channel != 0xFFFF)
        {   // swap to channel mode
            gEeprom.ScreenChannel[gEeprom.TX_VFO] = Channel;
            #ifdef ENABLE_VOICE
                AUDIO_SetVoiceID(0, VOICE_ID_CHANNEL_MODE);
                AUDIO_SetDigitVoice(1, Channel + 1);
                gAnotherVoiceID = (VOICE_ID_t)0xFE;
            #endif
            gRequestSaveVFO     = true;
            gVfoConfigureMode   = VFO_CONFIGURE_RELOAD;
            return;
        }
    }
}