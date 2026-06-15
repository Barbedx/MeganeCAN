#include "SerialConsole.h"
#include <Arduino.h>
#include <SerialCommands.h>
#include <can_common.h>
#include <esp32_can.h>
#include "../display/AffaDisplayBase.h"
#include "../display/AffaCommonConstants.h"
#include "../bluetooth.h"
#include "../apple_media_service.h"
#include "../effects/ScrollEffect.h"

// main-side globals this console drives.
extern AffaDisplayBase *display;
extern String btMode;
extern void gotFrame(CAN_FRAME *frame);

namespace {

void cmd_enable(SerialCommands *sender)
{
    Serial.println("Enabling display");
    display->setState(true);
}
void cmd_disable(SerialCommands *sender) { display->setState(false); }
void cmd_clearbonds(SerialCommands *sender)
{
    Serial.println("[BT] Clearing BLE bonds via serial command...");
    if (btMode == "ams")
        Bluetooth::ClearBonds();
    else
        Serial.println("[BT] ClearBonds only available in AMS mode");
}
void cmd_playpause(SerialCommands *sender)
{
    if (btMode == "ams" && Bluetooth::IsConnected())
        AppleMediaService::Toggle();
    else
        Serial.println("[BT] Not connected in AMS mode");
}
void cmd_next(SerialCommands *sender)
{
    if (btMode == "ams" && Bluetooth::IsConnected())
        AppleMediaService::NextTrack();
    else
        Serial.println("[BT] Not connected in AMS mode");
}
void cmd_prev(SerialCommands *sender)
{
    if (btMode == "ams" && Bluetooth::IsConnected())
        AppleMediaService::PrevTrack();
    else
        Serial.println("[BT] Not connected in AMS mode");
}
void cmd_scrollmtx(SerialCommands *sender)
{
    const char *text = sender->Next();
    const char *delayStr = sender->Next();
    if (!text)
        return;
    uint16_t delayMs = 300;
    if (delayStr)
    {
        delayMs = atoi(delayStr);
        if (delayMs < 20)
            delayMs = 20;
    }
    Serial.println("Scrolling text: ");
    Serial.println(text);
    ScrollEffect(display, ScrollDirection::Right, text, delayMs);
}
void cmd_scrollmtxl(SerialCommands *sender)
{
    const char *text = sender->Next();
    const char *delayStr = sender->Next();
    if (!text)
        return;
    uint16_t delayMs = 300;
    if (delayStr)
    {
        delayMs = atoi(delayStr);
        if (delayMs < 20)
            delayMs = 20;
    }
    ScrollEffect(display, ScrollDirection::Left, text, delayMs);
}
void cmd_setTime(SerialCommands *sender)
{
    char *timeStr = sender->Next();
    if (!timeStr)
    {
        Serial.println("Usage: st <HHMM>");
        return;
    }
    display->setTime(timeStr);
}

// "@INJ <id-hex> <b0> ..." injects a CAN frame into the same RX path as a real bus
// frame (gotFrame -> display->recv), so the bench can ACK the display handshake.
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

// "@EMU <0|1>" toggles bench emulator self-ACK.
void cmd_emu(SerialCommands *sender)
{
    char *v = sender->Next();
    bool on = v && atoi(v) != 0;
    if (display) display->setEmuSelfAck(on);
    Serial.printf("@EMU self-ACK = %d\n", on);
}

SerialCommand cmd_inj("@INJ", cmd_inject);
SerialCommand cmd_emu_c("@EMU", cmd_emu);
SerialCommand cmd_e("e", cmd_enable);
SerialCommand cmd_d("d", cmd_disable);
SerialCommand cmd_st("st", cmd_setTime);
SerialCommand cmd_msr("msr", cmd_scrollmtx);
SerialCommand cmd_msl("msl", cmd_scrollmtxl);
SerialCommand cmd_cb("cb", cmd_clearbonds);
SerialCommand cmd_pp("pp", cmd_playpause);
SerialCommand cmd_nx("nx", cmd_next);
SerialCommand cmd_pv("pv", cmd_prev);

char serial_command_buffer[64];
char serial_delim[] = " \r\n";
SerialCommands serialCommands(&Serial, serial_command_buffer, sizeof(serial_command_buffer), serial_delim);

} // namespace

void SerialConsole::begin()
{
    Serial.begin(115200);
    delay(2000);
    Serial.println("------------------------");
    Serial.println("   MEGANE CAN BUS       ");
    Serial.println("------------------------");
    serialCommands.AddCommand(&cmd_inj);
    serialCommands.AddCommand(&cmd_emu_c);
    serialCommands.AddCommand(&cmd_e);
    serialCommands.AddCommand(&cmd_d);
    serialCommands.AddCommand(&cmd_st);
    serialCommands.AddCommand(&cmd_msr);
    serialCommands.AddCommand(&cmd_msl);
    serialCommands.AddCommand(&cmd_cb);
    serialCommands.AddCommand(&cmd_pp);
    serialCommands.AddCommand(&cmd_nx);
    serialCommands.AddCommand(&cmd_pv);
}

void SerialConsole::loop()
{
    serialCommands.ReadSerial();
}
