#pragma once
#include <stdint.h>

// LinkProto v1 — the MM (ESP32-C3) <-> VH (WROVER) inter-board protocol
// (ARCHITECTURE-V2 §4). Wire: COBS-encoded frames delimited by 0x00; the
// pre-COBS frame is [ver:1][type:1][seq:1][payload…][crc16-ccitt BE:2], CRC
// over ver..payload. Unknown types are skipped (forward compatible).
namespace LinkProto
{
    constexpr uint8_t  VERSION       = 1;
    constexpr uint16_t MAX_FRAME     = 128; // pre-COBS: header + payload + crc
    constexpr uint16_t HEADER_LEN    = 3;   // ver, type, seq
    constexpr uint16_t CRC_LEN       = 2;
    constexpr uint16_t MAX_PAYLOAD   = MAX_FRAME - HEADER_LEN - CRC_LEN; // 123

    constexpr uint32_t PING_INTERVAL_MS = 1000;
    constexpr uint32_t PEER_TIMEOUT_MS  = 3000; // no valid frame for 3s -> down

    enum Type : uint8_t {
        HELLO      = 0x01, // {proto_ver:1, fw_ver:2 LE, caps:4 LE}
        PING       = 0x02, // empty
        PONG       = 0x03, // empty
        SIG_BATCH  = 0x10, // N x {sig_id:1, value:i32 LE}
        MEDIA_TEXT = 0x11, // {field:1, utf8 text...}
        HU_STATUS  = 0x12, // {linkUp:1, source:1, volume:1, freq_x10:2 LE}
        KEY_EVT    = 0x20, // {affaKey:2 LE, edge:1 (0=release,1=press,2=long)}
        RAW_FRAME  = 0x30, // {bus:1, id:2 LE, dlc:1, data...}
        TIME       = 0x40, // {unix:4 LE} clock sync (MM has CTS/NTP)
        LOG        = 0x41, // text line (throttled)
        CFG_GET    = 0x50, // {key\0} -> CFG_ACK
        CFG_SET    = 0x51, // {key\0value} -> CFG_ACK
        CFG_ACK    = 0x52, // {ok:1, key\0value}
        CAP_CTL    = 0x58, // {op:1 (0=stop,1=start), mode:1, seconds:2 LE}
        FILE_LS    = 0x60, // req: empty; resp: N x {size:4 LE, name\0}
        FILE_REQ   = 0x61, // {offset:4 LE, name\0}
        FILE_DATA  = 0x62, // {offset:4 LE, total:4 LE, data...} (empty data = EOF)
        FILE_ACK   = 0x63, // {offset:4 LE} flow control
        OTA_BEGIN  = 0x70, // {size:4 LE}
        OTA_DATA   = 0x71, // {offset:4 LE, data...}
        OTA_END    = 0x72, // {crc? reserved}
        OTA_STAT   = 0x73, // {code:1 (0=ok/ack, else error), detail:4 LE}
    };

    // MEDIA_TEXT field ids
    enum MediaField : uint8_t {
        MF_TITLE = 0, MF_ARTIST = 1, MF_ALBUM = 2, MF_SOURCE = 3, MF_STATE = 4,
    };

    // TX priority classes — a full queue drops the lowest class first
    // (ARCHITECTURE-V2 §4: LOG first, then RAW_FRAME; keys pre-empt everything).
    enum Prio : uint8_t {
        PRIO_LOW  = 0,   // LOG
        PRIO_MID  = 1,   // RAW_FRAME, SIG_BATCH
        PRIO_HIGH = 2,   // control, keys, heartbeat, OTA/file
    };

    inline Prio defaultPrio(uint8_t type)
    {
        switch (type) {
            case LOG:       return PRIO_LOW;
            case RAW_FRAME:
            case SIG_BATCH: return PRIO_MID;
            default:        return PRIO_HIGH;
        }
    }
}
