#ifndef Affa3Display_H
#define Affa3Display_H

#include <Arduino.h>

#define AFFA3_PACKET_LEN 0x08
#define AFFA3_KEY_LOAD 0x0000 /* This at the bottom of the remote;) */
#define AFFA3_KEY_SRC_RIGHT 0x0001
#define AFFA3_KEY_SRC_LEFT 0x0002
#define AFFA3_KEY_VOLUME_UP 0x0003
#define AFFA3_KEY_VOLUME_DOWN 0x0004
#define AFFA3_KEY_PAUSE 0x0005
#define AFFA3_KEY_ROLL_UP 0x0101
#define AFFA3_KEY_ROLL_DOWN 0x0141
#define AFFA3_KEY_HOLD_MASK (0x80 | 0x40) 
#define AFFA3_PING_TIMEOUT 5000 // 5 seconds, or adjust as needed

namespace Affa3Display {

    


// Forward declaration of sendCan
    void sendCan(uint32_t id, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7);

    void showMenu(
    const char* header, 
    const char* item1, 
    const char* item2, 
    uint8_t firstFrameSize = 0x5A, 
    uint8_t scrollLockIndicator = 0x0B,
    uint8_t selectionItem1 = 0x00, 
    uint8_t selectionItem2 = 0x01
    );

    void showInfoMenu(
    const char* item1 = "AUX   AUTO",
    const char* item2 = "AF ON",
    const char* item3 = "SPEED 0",
    uint8_t offset1 = 0x41,
    uint8_t offset2 = 0x44,
    uint8_t offset3 = 0x48, 
    uint8_t infoPrefix = 0x60
    );

    void emulateKey(uint16_t key, bool hold = false );

    void display_Control( int8_t state);
    void setTime(char* clock);
    void sendTextToDisplay(
        uint8_t mode,
        uint8_t rdsIcon,
        uint8_t sourceIcon,
        uint8_t channel,
        
        const char* rawText
    );

    void sendDisplayHeader(
        uint8_t mode,            // byte3 - 74: full, 77: partial
        uint8_t rdsIcon,         // byte4 - 45: AF-RDS, 55: none
        uint8_t unknown,         // byte5 - usually 55
        uint8_t sourceIcon,      // byte6 - df: MANU, fd: PRESET, ff: NONE
        uint8_t textFormat,      // byte7 - 19–3F: radio, 59–7F: plain
        uint8_t controlByte      // byte8 - always 1
    );
    
    void sendDisplayFrame(uint8_t frameIndex, const char* textSegment);
    void scrollTextRightToLeft(
        uint8_t mode,
        uint8_t rdsIcon,
        uint8_t sourceIcon,
        uint8_t channel,
        const char* rawText,
        int speed = 300,
        int count = 1,
        const char* padding = "        "  // 8 spaces
    );
}
#endif
