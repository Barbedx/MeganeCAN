#pragma once
#include <cstdint>

namespace AffaCommon {

    static constexpr uint8_t KEY_HOLD_MASK = 0x80 | 0x40;
    static constexpr uint8_t PACKET_LENGTH = 8;
    static constexpr uint8_t PING_TIMEOUT = 0x81;

    enum FuncStatus : uint8_t {
        IDLE = 0x00,
        WAIT = 0x01,
        PARTIAL = 0x02,
        DONE = 0x03,
        ERROR = 0x04
    };

    enum IconFlags : uint8_t {
        ICON_NO_NEWS = 1 << 0,
        ICON_NEWS_ARROW = 1 << 1,
        ICON_NO_TRAFFIC = 1 << 2,
        ICON_TRAFFIC_ARROW = 1 << 3,
        ICON_NO_AFRDS = 1 << 4,
        ICON_AFRDS_ARROW = 1 << 5,
        ICON_NO_MODE = 1 << 6
    };

    enum class DisplayCtrl : uint8_t {
        Disable = 0x00,
        Enable  = 0x02
    };

    enum class AffaKey : uint16_t {
        Load        = 0x0000 /* This at the bottom of the remote;) */,
        SrcRight    = 0x0001,
        SrcLeft     = 0x0002,
        VolumeUp    = 0x0003,
        VolumeDown  = 0x0004,
        Pause       = 0x0005,
        RollUp      = 0x0101,
        RollDown    = 0x0141
    };
    inline constexpr uint16_t to_uint16(AffaKey key) {
    return static_cast<uint16_t>(key);
}

    enum class SyncStatus : uint8_t {
        FAILED     = 0x01,
        PEER_ALIVE = 0x02,
        START      = 0x04,
        FUNCSREG   = 0x08
    };

    enum class AffaError : uint8_t {
        NoError     = 0,
        NoSync      = 0x01,
        UnknownFunc = 0x02,
        SendFailed  = 0x03,
        Timeout     = 0x04,
        StrTooLong  = 0x05
    };

    // Operator overloads
    inline SyncStatus operator|(SyncStatus a, SyncStatus b) {
        return static_cast<SyncStatus>(
            static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline SyncStatus operator&(SyncStatus a, SyncStatus b) {
        return static_cast<SyncStatus>(
            static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    inline SyncStatus& operator|=(SyncStatus& a, SyncStatus b) {
        a = a | b;
        return a;
    }

    inline SyncStatus& operator&=(SyncStatus& a, SyncStatus b) {
        a = a & b;
        return a;
    }

    inline SyncStatus operator~(SyncStatus a) {
        return static_cast<SyncStatus>(
            ~static_cast<uint8_t>(a));
    }

    template<typename Enum>
    inline bool hasFlag(Enum value, Enum flag) {
        using Underlying = typename std::underlying_type<Enum>::type;
        return (static_cast<Underlying>(value) & static_cast<Underlying>(flag)) == static_cast<Underlying>(flag);
    }

}