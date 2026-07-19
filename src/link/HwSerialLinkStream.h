#pragma once
#include <Arduino.h>
#include "LinkPort.h"

// ILinkStream over a HardwareSerial — the target-side adapter used by both
// boards (MM: UART1 GPIO21/20; VH: UART2 GPIO18/19). Non-blocking both ways:
// write() takes only what the TX FIFO has room for, LinkPort resumes the rest
// on the next service().
struct HwSerialLinkStream : ILinkStream {
    HardwareSerial* s = nullptr;

    void bind(HardwareSerial& hs) { s = &hs; }

    int read(uint8_t* buf, int maxLen) override {
        if (!s) return 0;
        int n = s->available();
        if (n <= 0) return 0;
        if (n > maxLen) n = maxLen;
        return (int)s->read(buf, n);
    }

    int write(const uint8_t* buf, int len) override {
        if (!s) return 0;
        int room = s->availableForWrite();
        if (room <= 0) return 0;
        if (len > room) len = room;
        return (int)s->write(buf, len);
    }
};
