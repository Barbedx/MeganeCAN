#pragma once
#include "Affa2Base.h"
#include "../../apple_media_service.h"
#include <Arduino.h>

// Affa2 8-segment display.
// Reuses all Affa2Base CAN logic; adds non-blocking track title scroll
// driven by tickMedia() / setMediaInfo().
class Affa2Display : public Affa2Base
{
public:
    Affa2Display() = default;

    void setMediaInfo(const AppleMediaService::MediaInformation &info) override;
    void tickMedia() override;

private:
    String   _scrollTitle;                              // padded full string to scroll
    uint16_t _scrollPos      = 0;                       // current window start
    uint32_t _lastScrollMs   = 0;

    static constexpr uint8_t  VISIBLE_CHARS       = 8;   // 8-segment display width
    static constexpr uint16_t SCROLL_INTERVAL_MS  = 400; // ms per step
};
