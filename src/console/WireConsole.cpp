#include "WireConsole.h"
#include <Arduino.h>
#include <can_common.h>
#include <esp32_can.h>
#include <string.h>
#include <stdlib.h>
#include "../utils/WireProto.h"
#include "../display/AffaDisplayBase.h"

// main-side globals this handler drives.
extern AffaDisplayBase* display;
extern void gotFrame(CAN_FRAME* frame);

// Line is "@TAG <args...>". Mirrors the @INJ/@EMU serial commands and adds @KEY,
// giving the phone the return channel the broken serial-input path never provided.
void WireConsole::handle(const char* line, void* /*ctx*/)
{
    char buf[96];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *save = nullptr;
    char *tag = strtok_r(buf, " \t", &save);
    if (!tag) return;

    if (strcmp(tag, WireProto::TAG_INJ) == 0)
    {
        char *idS = strtok_r(nullptr, " \t", &save);
        if (!idS) return;
        CAN_FRAME f;
        f.id = (uint32_t)strtoul(idS, nullptr, 16);
        f.extended = false;
        f.rtr = false;
        f.length = 0;
        for (int i = 0; i < 8; i++)
        {
            char *b = strtok_r(nullptr, " \t", &save);
            if (!b) break;
            f.data.uint8[i] = (uint8_t)strtoul(b, nullptr, 16);
            f.length++;
        }
        Serial.printf("@INJ(ws) <- id=%03X len=%d\n", (unsigned)f.id, f.length);
        gotFrame(&f);
    }
    else if (strcmp(tag, WireProto::TAG_EV) == 0)
    {
        // reserved
    }
    else if (strcmp(tag, "@EMU") == 0)
    {
        char *v = strtok_r(nullptr, " \t", &save);
        bool on = v && atoi(v) != 0;
        if (display) display->setEmuSelfAck(on);
        Serial.printf("@EMU(ws) self-ACK = %d\n", on);
    }
    else if (strcmp(tag, WireProto::TAG_KEY) == 0)
    {
        char *cS = strtok_r(nullptr, " \t", &save);
        char *hS = strtok_r(nullptr, " \t", &save);
        if (!cS) return;
        uint16_t code = (uint16_t)atoi(cS);   // decimal, matches /emulate/key
        bool hold = hS && atoi(hS) != 0;
        if (display) display->ProcessKey(static_cast<AffaCommon::AffaKey>(code), hold);
        Serial.printf("@KEY(ws) <- code=%u hold=%d\n", code, hold);
    }
}
