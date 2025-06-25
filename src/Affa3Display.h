// Step 4: Create Affa3Display.h/.cpp (AFFA3 implementation)
// File: Affa3Display.h
#pragma once
#include "IDisplay.h"
#include "CanUtils.h"
#ifdef HAS_MENU
#include "IMenuDisplay.h"
#endif 

class Affa3Display : public IDisplay
#ifdef HAS_MENU
, public IMenuDisplay
#endif
{
public:
    static constexpr uint16_t AFFA3_PACKET_REPLY_FLAG = 0x400;/* Bit oznaczajązy że jest to odpowiedź */

    static constexpr uint16_t PACKET_ID_SYNC_REPLY = 0x3CF;/* Pakiet uzywany w celu utrzymania synchronizacji od wyświetlacza */
    static constexpr uint16_t AFFA3_PACKET_ID_SYNC = 0x3DF;/* Pakiet uzywany w celu utrzymania synchronizacji od wyświetlacza */

    static constexpr uint16_t PACKET_ID_DISPLAY_CTRL = 0x1B1;/* Pakiet używany do kontroli wyświetlacza (włączanie/wyłączanie) */
    static constexpr uint16_t AFFA3_PACKET_ID_SETTEXT = 0x121;/* Pakiet uzywany do wysyłania tekstu na wyświetlacz */
    static constexpr uint16_t AFFA3_PACKET_ID_KEYPRESSED = 0x0A9;/* Pakiet używany do przesyłania kodu klawisza z pilota */
;/* Pakiet uzywany do wysyłania tekstu na wyświetlacz */

    
    static constexpr uint8_t PACKET_LENGTH = 8;
    static constexpr uint8_t PACKET_FILLER = 0x81;

    enum class SyncStatus : uint8_t {
    FAILED     = 0x01,
    PEER_ALIVE = 0x02,
    START      = 0x04,
    FUNCSREG   = 0x08
};

 
    enum FuncStatus : uint8_t {
            IDLE = 0x00,
            WAIT = 0x01,/* Czekamy na odpowiedź */
            PARTIAL = 0x02,/* Wyświetlacz otrzymał część danych */
            DONE = 0x03,/* Zakończono wykonywanie */
            ERROR = 0x04/* Wystąpił błąd */
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
    enum class Affa3Key : uint16_t {
    Load        = 0x0000,  // Ten na dole pilota ;)
    SrcRight    = 0x0001,
    SrcLeft     = 0x0002,
    VolumeUp    = 0x0003,
    VolumeDown  = 0x0004,
    Pause       = 0x0005,
    RollUp      = 0x0101,
    RollDown    = 0x0141
};

static constexpr uint8_t KEY_HOLD_MASK = 0x80 | 0x40;

    void tick() override;
    void recv(CAN_FRAME* frame) override;
    Affa3Error setText(const char* text, uint8_t digit=255 /* 0-9, or anything else for none */) override;
    Affa3Error scrollText(const char* text, uint16_t  delayMs /* 0-9, or anything else for none */) override;
    Affa3Error setState(bool enabled) override;  
#ifdef HAS_MENU
    void setMenu(const char* title, const char* items[], uint8_t count) override;
#endif
};
    inline Affa3Display::SyncStatus operator|(Affa3Display::SyncStatus a, Affa3Display::SyncStatus b) {
        return static_cast<Affa3Display::SyncStatus>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline Affa3Display::SyncStatus operator&(Affa3Display::SyncStatus a, Affa3Display::SyncStatus b) {
        return static_cast<Affa3Display::SyncStatus>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    inline Affa3Display::SyncStatus& operator|=(Affa3Display::SyncStatus& a, Affa3Display::SyncStatus b) {
        a = a | b;
        return a;
    }

    // Bitwise NOT
    inline Affa3Display::SyncStatus operator~(Affa3Display::SyncStatus a) {
        return static_cast<Affa3Display::SyncStatus>(
            ~static_cast<uint8_t>(a)
        );
    }
    // AND-assign
    inline Affa3Display::SyncStatus& operator&=(Affa3Display::SyncStatus& a, Affa3Display::SyncStatus b) {
        a = a & b;
        return a;
    }


    template<typename Enum>
inline bool hasFlag(Enum value, Enum flag) {
    using Underlying = typename std::underlying_type<Enum>::type;
    return (static_cast<Underlying>(value) & static_cast<Underlying>(flag)) == static_cast<Underlying>(flag);
}