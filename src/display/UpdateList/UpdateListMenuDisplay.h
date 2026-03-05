#pragma once
#include "UpdateListDisplay.h"

// UpdateList LCD (menu-capable) display.
// Uses the same UpdateList CAN protocol as UpdateListDisplay but sends
// text using the LCD channel/location encoding from the archive affa3 library.
// Inherits setMediaInfo() + tickMedia() from UpdateListDisplay so Spotify
// track scrolling works without duplication.
// Selected via NVS display_type = "updatelist_menu".
class UpdateListMenuDisplay : public UpdateListDisplay
{
public:
    UpdateListMenuDisplay() = default;

    AffaCommon::AffaError setText(const char *text, uint8_t digit = 255) override;
};
