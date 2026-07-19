#pragma once
#include <stdint.h>

// LinkProto — the RUNTIME inter-board protocol (ARCHITECTURE-V2 §4). Wire:
// COBS-encoded frames delimited by 0x00; the pre-COBS frame is
// [ver:1][type:1][seq:1][payload…][crc16-ccitt BE:2], CRC over ver..payload.
// Unknown types are skipped (forward compatible).
//
// CR-01: firmware upload/recovery lives in the separate, independently
// versioned ProgProto.h (type range 0x70-0x7F, same byte transport).
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
        DISP_CMD   = 0x21, // {op:1 (DispOp), args...} — GW steers the peripheral's display
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
        // 0x70-0x7F reserved for the programming transport — see ProgProto.h (CR-01).
    };

    // HELLO capability bitmap (CR-05): what the peer can do. GW adapts features
    // to the attached peripheral from this — never from its name or an ifdef.
    // CAP_ prefix: bare names collide with Arduino.h macros (DISPLAY, …).
    namespace Caps {
        constexpr uint32_t CAP_DISPLAY  = 0x01;  // drives an OEM display
        constexpr uint32_t CAP_CAN      = 0x02;  // owns a CAN interface
        constexpr uint32_t CAP_BLE      = 0x04;  // BLE roles (AMS/HID)
        constexpr uint32_t CAP_LOGGER   = 0x08;  // FS capture/logging
        constexpr uint32_t CAP_OTA      = 0x10;  // accepts ProgProto flashing
        constexpr uint32_t CAP_KEYBOARD = 0x20;  // emits key events
        constexpr uint32_t CAP_MEDIA    = 0x40;  // media source/render
        constexpr uint32_t CAP_WEB      = 0x80;  // hosts the web UI
    }

    // MEDIA_TEXT field ids
    enum MediaField : uint8_t {
        MF_TITLE = 0, MF_ARTIST = 1, MF_ALBUM = 2, MF_SOURCE = 3, MF_STATE = 4,
    };

    // DISP_CMD ops — the IDisplay surface serialized (GW's RemoteDisplay sends,
    // the peripheral's DISP_CMD server executes on its local driver). Strings
    // are NUL-terminated and packed back to back.
    enum DispOp : uint8_t {
        DO_SET_TEXT   = 1,  // {digit:1, text...}
        DO_SET_STATE  = 2,  // {on:1}
        DO_SET_TIME   = 3,  // {"HHMM"}
        DO_SHOW_MENU  = 4,  // {scroll:1, header\0 item1\0 item2\0}
        DO_INFO_POPUP = 5,  // {l1\0 l2\0 l3\0}
        DO_HIDE_INFO  = 6,  // {}
        DO_CONFIRM    = 7,  // {cap\0 r1\0 r2\0}
        DO_FULLSCREEN = 8,  // {l1\0 l2\0 l3\0}
        DO_HIDE_FULL  = 9,  // {}
        DO_POPUP_TEXT = 10, // {icon:1, srcIcon:1, fmt:1, text...}
        DO_HIDE_POPUP = 11, // {}
        DO_KEY        = 12, // {affaKey:2 LE, hold:1} — inject into the display's ProcessKey
        DO_AUX        = 13, // {on:1}
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
