#include "SerialConsole.h"
#include <Arduino.h>
#include <SerialCommands.h>
#include <can_common.h>
#include <esp32_can.h>
#include "../display/AffaDisplayBase.h"
#include "../display/AffaCommonConstants.h"
#include "../utils/CanUtils.h"
#include "../utils/Log.h"        // Log::setLevel (the `vb` alias target)
#include "../bluetooth.h"
#include "../apple_media_service.h"
#include "../effects/ScrollEffect.h"

// main-side globals this console drives.
extern AffaDisplayBase *display;
extern String btMode;
extern void gotFrame(CAN_FRAME *frame);

namespace {

// The proxy buttons / freeform box send screen text through the SerialCommands
// tokenizer, which splits on spaces — so every field must be a single space-free
// token. Convention: the sender encodes spaces as '_' and an empty field as '~'.
// This reverses it back into the real string.
String decodeField(const char *tok)
{
    if (!tok || strcmp(tok, "~") == 0)
        return String("");
    String s(tok);
    s.replace('_', ' ');
    return s;
}

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

// ---- Screen-state commands (mirror the /affa3/setMenu, /api/info, /api/confirm HTTP
// routes) so the whole display surface is drivable from serial — no WiFi needed. Text
// fields are decodeField()'d (spaces as '_', empty as '~').

// "menu <caption> <item1> <item2> [scrollHex]" — the menu / now-playing screen.
// scrollHex: 00 none / 0B down / 07 up / 0C both (default 0B).
void cmd_menu(SerialCommands *sender)
{
    if (!display) return;
    String cap = decodeField(sender->Next());
    String i1  = decodeField(sender->Next());
    String i2  = decodeField(sender->Next());
    const char *scrollTok = sender->Next();
    uint8_t scroll = 0x0B;
    if (scrollTok && *scrollTok)
        scroll = (uint8_t)strtoul(scrollTok, nullptr, 16);
    Serial.printf("[serial menu] cap='%s' i1='%s' i2='%s' scroll=0x%02X\n",
                  cap.c_str(), i1.c_str(), i2.c_str(), scroll);
    display->showMenu(cap.c_str(), i1.c_str(), i2.c_str(), scroll);
}

// "info <line1> <line2> <line3>" — the 3-line info popup (8 chars/slot).
void cmd_info(SerialCommands *sender)
{
    if (!display) return;
    String l1 = decodeField(sender->Next());
    String l2 = decodeField(sender->Next());
    String l3 = decodeField(sender->Next());
    Serial.printf("[serial info] '%s' '%s' '%s'\n", l1.c_str(), l2.c_str(), l3.c_str());
    display->showInfoPopup(l1.c_str(), l2.c_str(), l3.c_str());
}

// "infox" — close/hide the info popup.
void cmd_infox(SerialCommands *sender)
{
    if (!display) return;
    Serial.println("[serial info] close");
    display->hideInfoPopup();
}

// "popup <caption> <row1> <row2>" — the big confirm box.
void cmd_popup(SerialCommands *sender)
{
    if (!display) return;
    String cap = decodeField(sender->Next());
    String r1  = decodeField(sender->Next());
    String r2  = decodeField(sender->Next());
    Serial.printf("[serial popup] cap='%s' r1='%s' r2='%s'\n",
                  cap.c_str(), r1.c_str(), r2.c_str());
    display->showConfirmBox(cap.c_str(), r1.c_str(), r2.c_str());
}

// "full <line1> <line2> <line3>" — fullscreen big-text screen (0x21 mode 0x05), e.g.
// the OEM "Please insert / navigation CD". Lines are \r-joined and centred-ish.
void cmd_full(SerialCommands *sender)
{
    if (!display) return;
    String l1 = decodeField(sender->Next());
    String l2 = decodeField(sender->Next());
    String l3 = decodeField(sender->Next());
    Serial.printf("[serial full] '%s' '%s' '%s'\n", l1.c_str(), l2.c_str(), l3.c_str());
    display->showFullscreenText(l1.c_str(), l2.c_str(), l3.c_str());
}

// "fclose" — dismiss the fullscreen text (close full window).
void cmd_fclose(SerialCommands *sender)
{
    if (!display) return;
    Serial.println("[serial full] close");
    display->hideFullscreenText();
}

// "txt <text> [digit]" — short radio text line.
void cmd_txt(SerialCommands *sender)
{
    if (!display) return;
    String t = decodeField(sender->Next());
    const char *digitTok = sender->Next();
    uint8_t digit = 255;
    if (digitTok && *digitTok)
        digit = (uint8_t)atoi(digitTok);
    Serial.printf("[serial txt] '%s' digit=%d\n", t.c_str(), digit);
    display->setText(t.c_str(), digit);
}

// "vb <0|1>" — back-compat alias: maps to the leveled logger. vb 1 => DBG (shows the
// AFFA3 ISO-TP narration), vb 0 => INF. (Prefer /api/loglevel?n=0..4; serial INPUT is
// dead on the C3 USB-CDC, so this only fires on boards whose serial RX works.)
void cmd_verbose(SerialCommands *sender)
{
    const char *v = sender->Next();
    bool on = v && atoi(v) != 0;
    Log::setLevel(on ? LogLevel::DBG : LogLevel::INF);
    Serial.printf("[serial] log level = %s\n", Log::levelName(Log::level()));
}

// "tx <idhex> <b0> [b1..b7]" — transmit ONE raw CAN frame onto the bus (DLC = byte
// count, 1..8). The core protocol-RE primitive: poke the panel/radio with arbitrary
// bytes and watch the [RX] answers. Goes through CanUtils::sendFrame, so it mirrors
// @TX and is busAlive-gated (suppressed on a dead bench bus). To replay a multi-frame
// ISO-TP sequence, send each frame (10.., 21.., 22..) as its own `tx` line.
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
    Serial.printf("[serial tx] id=0x%03X len=%d ->\n", (unsigned)f.id, f.length);
    CanUtils::sendFrame(f);
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
SerialCommand cmd_menu_c("menu", cmd_menu);
SerialCommand cmd_info_c("info", cmd_info);
SerialCommand cmd_infox_c("infox", cmd_infox);
SerialCommand cmd_popup_c("popup", cmd_popup);
SerialCommand cmd_full_c("full", cmd_full);
SerialCommand cmd_fclose_c("fclose", cmd_fclose);
SerialCommand cmd_txt_c("txt", cmd_txt);
SerialCommand cmd_vb_c("vb", cmd_verbose);
SerialCommand cmd_tx_c("tx", cmd_tx);

// 160 bytes: the new multi-field screen commands (e.g. "popup <caption> <row1> <row2>"
// with underscore-encoded text) easily exceed the old 64-byte line buffer.
char serial_command_buffer[160];
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
    serialCommands.AddCommand(&cmd_menu_c);
    serialCommands.AddCommand(&cmd_info_c);
    serialCommands.AddCommand(&cmd_infox_c);
    serialCommands.AddCommand(&cmd_popup_c);
    serialCommands.AddCommand(&cmd_full_c);
    serialCommands.AddCommand(&cmd_fclose_c);
    serialCommands.AddCommand(&cmd_txt_c);
    serialCommands.AddCommand(&cmd_vb_c);
    serialCommands.AddCommand(&cmd_tx_c);
}

void SerialConsole::loop()
{
    serialCommands.ReadSerial();
}
