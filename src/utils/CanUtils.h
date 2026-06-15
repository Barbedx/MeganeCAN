#pragma once
#pragma once
#include <can_common.h>
#include <esp32_can.h>
#include <Arduino.h>

class CanUtils {
public:
    static void sendCan(uint32_t id, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                        uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7);
    
    static void sendMsgBuf(uint32_t id, const uint8_t* data, uint8_t len = 8);
    static void sendFrame(CAN_FRAME &frame);
    static void printCanFrame(const CAN_FRAME &frame, bool isOutgoing);

    // Live-bus gate. We only transmit once we've actually received CAN traffic,
    // proving a transceiver + live bus is present. Without it (e.g. the bench
    // board, no transceiver), unACKed TX drives the controller BUS_OFF and the
    // esp32_can watchdog's auto-recovery underflows the IDF TWAI tx_msg_count
    // (assert twai.c:184) -> reboot loop. noteRxActivity() is called for every
    // received frame; busAlive() is true while traffic is recent.
    static void noteRxActivity();
    static bool busAlive();
};