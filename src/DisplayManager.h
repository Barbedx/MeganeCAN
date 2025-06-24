#pragma once
#include <Arduino.h>
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */

class DisplayManager {
public:
    DisplayManager();

    void registerDisplay();
    void initDisplay();
    void enableDisplay();
    void startSync();
    void syncOK();
    void syncDisp();
    void messageTest();
    void messageTest2();
    void messageWelcome();
    // This method will be called from your CAN callback when frame 0x3CF arrives
    void handleFrame_0x3CF(const CAN_FRAME* frame);

    bool isSessionStarted() const;

private:
    bool sessionStarted = false;
    unsigned long lastPingTime = 0;

    // Internal helpers to send specific messages
    void sendRegisterDisplay();
    void sendInitDisplay();
};
