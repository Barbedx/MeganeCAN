#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
#include "Affa3Constants.h" /* Constants related to Affa3 */
class IDisplay {
public:
    virtual void tick() = 0;
    virtual void recv(CAN_FRAME* frame) = 0;
    virtual Affa3::Affa3Error setText(const char* text, uint8_t digit =255 /* 0-9, or anything else for none */) ;
    virtual Affa3::Affa3Error scrollText(const char* text, uint16_t delayMs =300/* 0-9, or anything else for none */) ;

    virtual Affa3::Affa3Error setState(bool enabled) = 0; 
};

