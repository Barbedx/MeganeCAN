#include "DispConsole.h"
#include "DispLink.h"
#include <Arduino.h>
#include <SerialCommands.h>
#include <can_common.h>
#include <esp32_can.h>
#include "../display/AffaDisplayBase.h"
#include "../utils/CanUtils.h"
#include "../link/LinkProto.h"

extern AffaDisplayBase *display;
extern void gotFrame(CAN_FRAME *frame);

namespace
{
    void cmd_inject(SerialCommands *sender)
    {
        char *idStr = sender->Next();
        if (!idStr) { Serial.println("@INJ: missing id"); return; }
        CAN_FRAME f;
        f.id = (uint32_t)strtol(idStr, nullptr, 16);
        f.extended = false;
        f.rtr = false;
        f.length = 0;
        for (int i = 0; i < 8; i++)
        {
            char *b = sender->Next();
            if (!b) break;
            f.data.uint8[i] = (uint8_t)strtol(b, nullptr, 16);
            f.length++;
        }
        Serial.printf("@INJ <- id=%03X len=%d\n", (unsigned)f.id, f.length);
        gotFrame(&f);
    }

    void cmd_emu(SerialCommands *sender)
    {
        char *v = sender->Next();
        bool on = v && atoi(v) != 0;
        if (display) display->setEmuSelfAck(on);
        Serial.printf("@EMU self-ACK = %d\n", on);
    }

    void cmd_tx(SerialCommands *sender)
    {
        char *idStr = sender->Next();
        if (!idStr) { Serial.println("usage: tx <idhex> <b0..b7>"); return; }
        CAN_FRAME f;
        f.id = (uint32_t)strtoul(idStr, nullptr, 16);
        f.extended = false;
        f.rtr = false;
        f.length = 0;
        for (int i = 0; i < 8; i++)
        {
            char *b = sender->Next();
            if (!b) break;
            f.data.uint8[i] = (uint8_t)strtoul(b, nullptr, 16);
            f.length++;
        }
        Serial.printf("[tx] id=0x%03X len=%d\n", (unsigned)f.id, f.length);
        CanUtils::sendFrame(f);
    }

    // "@PROG <hex>" — one raw link frame, type byte first. Max frame = link
    // payload cap, so the hex line fits comfortably in the buffer below.
    void cmd_prog(SerialCommands *sender)
    {
        char *hex = sender->Next();
        if (!hex) { Serial.println("@PROG: missing hex"); return; }
        size_t hl = strlen(hex);
        if (hl < 2 || (hl & 1)) { Serial.println("@PROG: bad hex"); return; }
        uint8_t buf[LinkProto::MAX_PAYLOAD + 1];
        uint16_t n = 0;
        for (size_t i = 0; i + 1 < hl && n < sizeof(buf); i += 2)
        {
            char two[3] = { hex[i], hex[i + 1], 0 };
            buf[n++] = (uint8_t)strtoul(two, nullptr, 16);
        }
        if (!DispLink::sendRaw(buf[0], buf + 1, (uint16_t)(n - 1)))
            Serial.println("@PROG: link down / queue full");
    }

    SerialCommand c_inj("@INJ", cmd_inject);
    SerialCommand c_emu("@EMU", cmd_emu);
    SerialCommand c_tx("tx", cmd_tx);
    SerialCommand c_prog("@PROG", cmd_prog);

    // @PROG lines carry up to ~256 hex chars (128B frame) + command word.
    char s_buf[320];
    char s_delim[] = " \r\n";
    SerialCommands s_commands(&Serial, s_buf, sizeof(s_buf), s_delim);
}

void DispConsole::begin()
{
    Serial.begin(115200);
    delay(1500);
    Serial.println("------------------------");
    Serial.println("  MEGANE CAN - DISP     ");
    Serial.println("------------------------");
    s_commands.AddCommand(&c_inj);
    s_commands.AddCommand(&c_emu);
    s_commands.AddCommand(&c_tx);
    s_commands.AddCommand(&c_prog);
}

void DispConsole::loop()
{
    s_commands.ReadSerial();
}
