#include "UpdateListMenuDisplay.h"
#include <string.h>

// LCD setText — uses the channel/location encoding from the archive affa3 library
// (affa3_do_set_text / affa3_display_full_screen style).
//
// Format (icons path, always sent so the display initialises correctly):
//   0x10         "set text" command
//   0x1C         text + icons subcommand
//   0x7F
//   0x55         icons: NO_TRAFFIC | NO_NEWS | NO_AFRDS | NO_MODE
//   0x55         literal separator
//   0xFF         mode = AFFA3_ICON_MODE_NONE
//   0x60         channel 0  (LCD encoding: 0x60 | chan, chan=0)
//   0x03         loc = AFFA3_LOCATION(0,0) | SELECTED | FULLSCREEN
//   [8 bytes]    old text (same as new for first write)
//   0x10         separator
//   [12 bytes]   new text
//   0x00         terminator
AffaCommon::AffaError UpdateListMenuDisplay::setText(const char *text, uint8_t /*digit*/)
{
    char oldBuf[8]  = {' ',' ',' ',' ',' ',' ',' ',' '};
    char newBuf[12] = {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};

    strncpy(oldBuf, text, sizeof(oldBuf));
    strncpy(newBuf, text, sizeof(newBuf));

    uint8_t data[32];
    uint8_t len = 0;

    data[len++] = 0x10;   // set text
    data[len++] = 0x1C;   // text + icons
    data[len++] = 0x7F;
    data[len++] = 0x55;   // icons: NO_TRAFFIC | NO_NEWS | NO_AFRDS | NO_MODE
    data[len++] = 0x55;
    data[len++] = 0xFF;   // mode = AFFA3_ICON_MODE_NONE
    data[len++] = 0x60;   // channel 0 (LCD: 0x60 | 0)
    data[len++] = 0x03;   // AFFA3_LOCATION(0,0) | SELECTED | FULLSCREEN

    for (uint8_t i = 0; i < 8; i++)
        data[len++] = oldBuf[i];

    data[len++] = 0x10;   // separator

    for (uint8_t i = 0; i < 12; i++)
        data[len++] = newBuf[i];

    data[len++] = 0x00;   // terminator

    return affa3_send(UpdateList::PACKET_ID_SETTEXT, data, len);
}
