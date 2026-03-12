#pragma once
#include "UpdateListBase.h"
#include <Arduino.h>
#include <bluetooth/TrackInfo.h>

// Affa2 8-segment display.
// Reuses all Affa2Base CAN logic; adds non-blocking track title scroll
// driven by tickMedia() / setMediaInfo().
class UpdateListDisplay : public UpdateListBase
{
public:
    UpdateListDisplay() = default;

    void setMediaInfo(const TrackInfo info) override;
    void tickMedia() override;
    void onBtDisconnected() override;

protected:
    void onRadioText(bool isAux) override;

private:
    String   _scrollTitle;                              // padded full string to scroll
    uint16_t _scrollPos      = 0;                       // current window start
    uint32_t _lastScrollMs   = 0;
    bool     _isPlaying      = false;                   // AMS playback state
    bool     _titleShownOnce = false;                   // static title sent after pause

    static constexpr uint8_t  VISIBLE_CHARS       = 8;   // 8-segment display width
    static constexpr uint16_t SCROLL_INTERVAL_MS  = 400; // ms per step
};
