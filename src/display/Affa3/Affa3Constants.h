 #pragma once
#include <cstdint>

namespace Affa3 {
    static constexpr uint16_t PACKET_REPLY_FLAG       = 0x400;
    static constexpr uint16_t PACKET_ID_SYNC_REPLY    = 0x3CF;
    static constexpr uint16_t PACKET_ID_SYNC          = 0x3DF;
    static constexpr uint16_t PACKET_ID_DISPLAY_CTRL  = 0x1B1;
    static constexpr uint16_t PACKET_ID_SETTEXT       = 0x121;
    static constexpr uint16_t PACKET_ID_KEYPRESSED    = 0x0A9;
    static constexpr uint8_t PACKET_FILLER = 0x81;

};