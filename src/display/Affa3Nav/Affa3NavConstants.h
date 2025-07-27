#pragma once
#include <cstdint>

namespace Affa3Nav {
    static constexpr uint16_t PACKET_REPLY_FLAG       = 0x400;
    static constexpr uint16_t PACKET_ID_SYNC_REPLY    = 0x3CF;
    static constexpr uint16_t PACKET_ID_SYNC          = 0x3AF;
    static constexpr uint16_t PACKET_ID_DISPLAY_CTRL  = 0x151;
    static constexpr uint16_t PACKET_ID_SETTEXT       = 0x151;
    static constexpr uint16_t PACKET_ID_KEYPRESSED    = 0x1C1;
    static constexpr uint8_t PACKET_FILLER = 0x00;
    static constexpr uint16_t PACKET_ID_NAV          = 0x1F1;
    
    enum class DisplayCtrl : uint8_t {
        Disable = 0x00,
        Enable  = 0x09
    };
    enum ScrollLockIndicator : uint8_t {
    SCROLL_NONE      = 0x00, // no scroll lock
    SCROLL_UP        = 0x07, // scroll UP
    SCROLL_DOWN      = 0x0B, // scroll DOWN
    SCROLL_BOTH      = 0x0C  // scroll UP and DOWN
};
}