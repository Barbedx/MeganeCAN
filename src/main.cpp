#include <Arduino.h>
#include <time.h>
#include "effects/ScrollEffect.h"
#include <stdlib.h>               // for rand()
#include "server/HttpServerManager.h"
#include <secrets.h>
#include <WiFi.h>
#include <ElegantOTA.h>
#include <SerialCommands.h>
// #include <PsychicHttp.h>

#include "ElmManager/MyELMManager.h"
#include "display/UpdateList/UpdateListDisplay.h"
#include "display/UpdateList/UpdateListMenuDisplay.h"
#include "display/UpdateList/UpdateListBase.h"
#include "display/Carminat/CarminatDisplay.h"
#include "bluetooth.h"
#include "apple_media_service.h"
#include "wifi_manager.h"
#include "utils/CanLog.h"
#include "utils/AppConfig.h"
#include "bus/HwCanBus.h"
#include "bus/SerialMirrorTap.h"
#include "wire/SerialWireLink.h"
#include "wire/WsWireLink.h"
#include "utils/WireProto.h"
#include "vdisplay/CarminatVirtualDisplay.h"
#include "vdisplay/UpdateListSegVirtualDisplay.h"
#include "vdisplay/UpdateListLcdVirtualDisplay.h"
#include "bus/ArduinoClock.h"
#include <string.h>
#include "BleMediaKeyboard.h"

AffaDisplayBase *display = nullptr;
unsigned long lastPingTime = 0;

// BT mode state (read from NVS in initDisplay, used throughout)
String btMode = "ams";   // "ams" or "keyboard"
bool _autoTime = true;   // sync display clock from CTS (AMS mode only)
bool _timeSyncDone = false; // reset each time BT disconnects
bool _elmEnabled = false; // ELM327 enabled (read from NVS, configurable via Web UI)

BleMediaKeyboard bleKeyboard;
Preferences preferences;
// HttpServerManager serverManager(*display, preferences);
HttpServerManager *serverManager = nullptr;

// WireProto links: UART (existing serial proxy) + WebSocket (phone live view/record).
SerialWireLink g_serialLink;
WsWireLink     g_wsLink;

// Mirror every received CAN frame to the WebSocket as @RX (rate-limited inside
// WsWireLink). Registered as a HwCanBus RX tap so it sees the live car bus.
struct WsRecorderTap : IBusTap {
    void onRx(const Frame& f) override { g_wsLink.emitRxFrame(f.id, f.data, f.len); }
};

// ---- FULL-EMULATION ---------------------------------------------------------------
// An in-firmware virtual display driven by the radio over the bus. When on, a real
// virtual display ACKs every outbound frame (PARTIAL), so the radio's affa3_do_send
// emits the whole multi-frame sequence on the bench WITHOUT the self-ACK hack and
// WITHOUT a real panel — and the ESP decodes its own screen (exposed at /api/screen).
// The "true closed loop": radio -> (tap) -> virtual display -> (ACK) -> radio recv.
// Default off; toggle via /api/fullemu?on=1.
// One virtual display per protocol; g_vd points at the one matching display_type
// (set in initDisplay, mirroring the radio-side `display`).
static CarminatVirtualDisplay      g_vdCarminat;
static UpdateListSegVirtualDisplay g_vdUlSeg;
static UpdateListLcdVirtualDisplay g_vdUlLcd;
static VirtualDisplayBase*         g_vd = &g_vdCarminat;
static bool g_fullEmu = false;

// Pick the virtual display that matches the emulated radio protocol.
void selectVirtualDisplay(const String& displayType) {
    if (displayType == "carminat")            g_vd = &g_vdCarminat;
    else if (displayType == "updatelist")     g_vd = &g_vdUlSeg;
    else if (displayType == "updatelist_menu") g_vd = &g_vdUlLcd;
    else                                       g_vd = &g_vdUlLcd;
}

// The virtual display's ACK/key sends are delivered straight into the radio's recv().
struct RadioRecvBus : ICanBus {
    bool send(const Frame& f) override {
        if (!display) return false;
        CAN_FRAME cf;
        cf.id = f.id; cf.extended = f.extended; cf.rtr = false; cf.length = f.len;
        for (int i = 0; i < f.len && i < 8; i++) cf.data.uint8[i] = f.data[i];
        display->recv(&cf);
        return true;
    }
    void onReceive(RxHandler, void*) override {}
    bool isLive() const override { return true; }
};
static RadioRecvBus g_radioRecvBus;

// Radio TX -> virtual display (gated by the toggle). Registered as a HwCanBus TX tap,
// so it sees every outbound frame before the (bench-suppressed) CAN0 send.
struct EmuFeedTap : IBusTap {
    void onTx(const Frame& f) override { if (g_fullEmu) g_vd->onCanRx(f); }
};
static EmuFeedTap g_emuFeedTap;

void setFullEmu(bool on) {
    if (on && !g_fullEmu) {
        g_vd->begin(g_radioRecvBus, defaultClock());
        g_vd->setAckMode(VirtualDisplayBase::ACK_PARTIAL);
        if (display) display->setEmuSelfAck(false);   // the virtual display ACKs now
    }
    g_fullEmu = on;
    Serial.printf("[fullemu] %s\n", on ? "ON — virtual display ACKs the radio" : "OFF");
}

static String jsonEscAscii(const char* s) {
    String o;
    for (const char* p = s; *p; ++p) {
        if (*p == '"' || *p == '\\') o += '\\';
        o += *p;
    }
    return o;
}

// JSON of the ESP's own decoded screen (only meaningful while full-emulation is on).
String fullEmuScreenJson() {
    const ScreenModel& s = g_vd->screen();
    String j = "{\"on\":";
    j += g_fullEmu ? "true" : "false";
    j += ",\"mode\":" + String((int)s.mode);
    j += ",\"header\":\"" + jsonEscAscii(s.header) + "\"";
    j += ",\"item0\":\"" + jsonEscAscii(s.item0) + "\"";
    j += ",\"item1\":\"" + jsonEscAscii(s.item1) + "\"";
    j += ",\"sel\":" + String(s.sel);
    j += ",\"scroll\":" + String(s.scroll) + "}";
    return j;
}
MyELMManager *elmManager = nullptr;

// AP fallback credentials (used by WiFiManager when no home network is saved/reachable)
const char *ssid = Soft_AP_WIFI_SSID;
const char *password = Soft_AP_WIFI_PASS;

void gotFrame(CAN_FRAME *frame)
{
    // Route every received frame through the bus: refreshes the live-bus gate
    // (replaces CanUtils::noteRxActivity) and fans out to RX taps. The radio-side
    // display still consumes CAN_FRAME directly (converted to Frame in later phases).
    HwCanBus::instance().ingest(*frame);
    if (frame->id != 0x3CF && frame->id != 0x3AF && frame->id != 0x7AF)
        CanUtils::printCanFrame(*frame, false);
    CanLog::onFrame(frame->id, frame->extended, frame->length, frame->data.uint8);
    display->recv(frame);
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
// void cmd_enable()    { affa3_display_ctrl(0x01) displayManager.enableDisplay(); }

// void cmd_messageTestold5() { displayManager.messageTest5(); }
//  void cmd_messageTestold6() { displayManager.messageTest6(); }
//  void cmd_messageTestold(){ displayManager.messageTest2(); }
//   void cmd_mgwelcome(){ displayManager.messageWelcome(); }
void cmd_scrollmtx(SerialCommands *sender)
{
    const char *text = sender->Next();
    const char *delayStr = sender->Next();

    if (!text)
    {
        // AFFA3_PRINT("Usage: ms <text> [delay_ms]\n");
        return;
    }

    uint16_t delayMs = 300; // default delay
    if (delayStr)
    {
        delayMs = atoi(delayStr);
        if (delayMs < 20)
            delayMs = 20; // clamp minimum
    }
    Serial.println("Scrolling text: ");
    Serial.println(text);
    ScrollEffect(display, ScrollDirection::Right, text, delayMs);

    // display.scrollText(text, delayMs);
}

void cmd_scrollmtxl(SerialCommands *sender)
{
    const char *text = sender->Next();
    const char *delayStr = sender->Next();

    if (!text)
    {
        // AFFA3_PRINT("Usage: ms <text> [delay_ms]\n");
        return;
    }

    uint16_t delayMs = 300; // default delay
    if (delayStr)
    {
        delayMs = atoi(delayStr);
        if (delayMs < 20)
            delayMs = 20; // clamp minimum
    }
    ScrollEffect(display, ScrollDirection::Left, text, delayMs);
}

void cmd_setTime(SerialCommands *sender)
{

    char *timeStr = sender->Next(); // e.g., "0930"
    if (!timeStr)
    {
        Serial.println("Usage: st <HHMM>");
        return;
    }
    display->setTime(timeStr); // unknown protocol
}

// Declare command handlers
// void cmd_enable(SerialCommands* sender);
// void cmd_disable(SerialCommands* sender);
// void cmd_setTime(SerialCommands* sender);
// void cmd_scrollmtx(SerialCommands* sender);
// void cmd_scrollmtxl(SerialCommands* sender);

// WireProto PC->fw: "@INJ <id-hex> <b0> <b1> ..." injects a CAN frame into the
// exact same RX path as a real bus frame (gotFrame -> display->recv). The PC-side
// emulator uses this to ACK the display handshake — reply on (sentId | 0x400) with
// 0x74 (DONE) / 30 01 00 (PARTIAL) — so affa3_do_send doesn't abort on its 2s
// no-display timeout and the ESP sends the full multi-frame payload. See WireProto.h.
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

// WireProto PC->fw: "@EMU <0|1>" toggles bench emulator self-ACK so multi-frame
// display sends emit their full real AFFA3 sequence (@TX) without a real display.
void cmd_emu(SerialCommands *sender)
{
    char *v = sender->Next();
    bool on = v && atoi(v) != 0;
    if (display) display->setEmuSelfAck(on);
    Serial.printf("@EMU self-ACK = %d\n", on);
}

// WireProto PC->fw command handler (delivered by a WireLink, today the WebSocket).
// Mirrors the @INJ/@EMU serial commands and adds @KEY, giving the phone the return
// channel the broken serial-input path never provided. Line is "@TAG <args...>".
void wireCommand(const char *line, void * /*ctx*/)
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

// Create command objects
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

// Create SerialCommands manager
char serial_command_buffer[64];
char serial_delim[] = " \r\n";
SerialCommands serialCommands(&Serial, serial_command_buffer, sizeof(serial_command_buffer), serial_delim);

void initSerial()
{

    // Initialize random seed (run only once)

    Serial.begin(115200);
    delay(2000);
    Serial.println("------------------------");
    Serial.println("   MEGANE CAN BUS       ");
    Serial.println("------------------------");
    // Setup commands
    // Register commands
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

void restoreDisplay(IDisplay &display, Preferences &prefs)
{

    prefs.begin("display", true);                           // read-only
    bool autoRestore = prefs.getBool("autoRestore", false); // default true
    if (!autoRestore)
    {
        prefs.end();
        Serial.println("Auto restore disabled by setting.");
        return;
    }
    Serial.println("Auto restore getted and is true.");
    String savedText = prefs.getString("lastText", "");
    String welcomeText = prefs.getString("welcomeText", "");
    prefs.end();

    display.setState(true);
    if (welcomeText.length() > 0)
    {
        ScrollEffect(&display, ScrollDirection::Left, welcomeText.c_str(), 250);
    }
    else
    {
        ScrollEffect(&display, ScrollDirection::Left, "                  Welcome to MEGANE 2", 250);
    }

    if (savedText.length() > 0)
    {
        display.setText(savedText.c_str());
    }
    else
    {
        if (random(0, 2) == 0)
        {
            display.setText("MEGANE");
        }
        else
        {
            display.setText("RENAULT");
        }
    }
}

void initDisplay()
{
    AppConfig::Load(); // read NVS "config" into RAM once (also creates the namespace -> no NOT_FOUND spam)
    String displayType = AppConfig::displayType;
    btMode      = AppConfig::btMode;
    _autoTime   = AppConfig::autoTime;
    _elmEnabled = AppConfig::elmEnabled;
    bool skipFuncReg = AppConfig::skipFuncReg;

    Serial.println("[Display Init] Display type: " + displayType);
    Serial.println("[Display Init] BT mode: " + btMode);
    Serial.println("[Display Init] Auto-time: " + String(_autoTime ? "on" : "off"));
    Serial.println("[Display Init] ELM enabled: " + String(_elmEnabled ? "yes" : "no"));
    Serial.printf("[Display Init] skip_funcreg raw value from NVS: %s\n", skipFuncReg ? "TRUE" : "FALSE (defaulted)");

    if (displayType == "carminat")
    {
        Serial.println("[Display Init] Instantiating CarminatDisplay");
        display = new CarminatDisplay();
    }
    else if (displayType == "updatelist")
    {
        Serial.println("[Display Init] Instantiating UpdateListDisplay (8-segment)");
        display = new UpdateListDisplay();
    }
    else if (displayType == "updatelist_menu")
    {
        Serial.println("[Display Init] Instantiating UpdateListMenuDisplay (full LED)");
        display = new UpdateListMenuDisplay();
    }
    else
    {
        Serial.println("[Display Init] Instantiating UpdateListBase (fallback)");
        display = new UpdateListBase();
    }

    selectVirtualDisplay(displayType);   // FULL-EMULATION twin matches the radio protocol

    display->setSkipFuncReg(skipFuncReg);
    Serial.println("[Display Init] Skip func-reg: " + String(skipFuncReg ? "yes" : "no"));

    // NOTE: display->begin() is called in setup() AFTER BT mode configuration
    elmManager = new MyELMManager(*display);
    serverManager = new HttpServerManager(*display, preferences);
    serverManager->attachElm(elmManager);
    serverManager->attachWire(&g_wsLink);   // register the /canstream WebSocket in begin()
    elmManager->loadHeaderConfig(preferences); // load per-header enable/disable from NVS
    if (display->isCarminat())
        static_cast<CarminatDisplay*>(display)->attachElm(elmManager);
    Serial.println("[Display Init] HttpServerManager initialized");
}
bool HandleKey(AffaCommon::AffaKey key, bool isHold)
{
    if (btMode == "ams")
    {
        if (!Bluetooth::IsConnected())
        {
            // Peripheral model: nothing to steer while disconnected — the phone
            // pairs/reconnects from iOS Settings, no candidate cycling here.
            return true;
        }

        switch (key)
        {
        case AffaCommon::AffaKey::Pause:    AppleMediaService::Toggle();    break;
        case AffaCommon::AffaKey::RollUp:   AppleMediaService::NextTrack(); break;
        case AffaCommon::AffaKey::RollDown: AppleMediaService::PrevTrack(); break;
        case AffaCommon::AffaKey::VolumeUp:
            if (isHold)
                for (int i = 0; i < 15; i++) AppleMediaService::VolumeUp();
            break;
        default: break;
        }
    }
    else // keyboard
    {
        switch (key)
        {
        case AffaCommon::AffaKey::Pause:    bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);     break;
        case AffaCommon::AffaKey::RollUp:   bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);     break;
        case AffaCommon::AffaKey::RollDown: bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK); break;
        case AffaCommon::AffaKey::VolumeUp:
            if (isHold) bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
            break;
        default: break;
        }
    }
    return true;
}

static AppleMediaService::MediaInformation g_mediaInfo;
void onDataUpdateCallback(const AppleMediaService::MediaInformation &info)
{
    // info.dump() is 8 serial lines; AMS fires often during playback, so throttle
    // the log to keep the serial channel readable. Display update runs every time.
    static uint32_t _lastDump = 0;
    if (millis() - _lastDump > 5000)
    {
        _lastDump = millis();
        info.dump();
    }
    display->setMediaInfo(info);
    g_mediaInfo = info;
}
void setup()
{
    delay(2000);
    initSerial();
    initDisplay();

    display->setKeyHandler(HandleKey);  // always set — HandleKey is mode-aware

    // On ESP32-C3 (single radio), bring BLE up before WiFi so the radio is free
    // during NimBLE startup.
    if (btMode == "ams")
    {
        AppleMediaService::RegisterForNotifications(
            onDataUpdateCallback,
            AppleMediaService::NotificationLevel::All);
        xTaskCreate([](void*) {
            Bluetooth::Begin("MCD1");
            Serial.println("[BT] AMS mode started");
            vTaskDelete(nullptr);
        }, "bt_begin", 16384, nullptr, 1, nullptr);
        Serial.println("[BT] AMS init launched in background");
    }
    else
    {
        bleKeyboard.begin("MeganeCAN");
        Serial.println("[BT] Keyboard mode started");
    }

    // Networking: join the saved home WiFi (STA + mDNS "meganecan.local" + optional
    // static IP) or fall back to the AP (secrets.h SSID) for config. Owns WiFi mode.
    WiFiManager::Begin(ssid, password, "meganecan");
    serverManager->begin();

    display->begin();

    CanLog::begin(); // load CAN-log config (enabled + ID filter) from NVS

    // WireProto fan-out: @TX/@RX to the UART (serial proxy) + the WebSocket (phone).
    WireProto::addLink(&g_serialLink);
    WireProto::addLink(&g_wsLink);
    WireProto::onCommand(wireCommand, nullptr);       // @KEY/@INJ/@EMU from the phone

    static SerialMirrorTap g_serialMirror;            // @TX mirror for the PC emulator
    HwCanBus::instance().addTap(&g_serialMirror);
    static WsRecorderTap g_wsRecorder;                // @RX live car-bus capture to WS
    HwCanBus::instance().addTap(&g_wsRecorder);
    HwCanBus::instance().addTap(&g_emuFeedTap);       // FULL-EMULATION feed (gated off)
    CAN0.setCANPins(GPIO_NUM_3, GPIO_NUM_4);
    CAN0.begin(CAN_BPS_500K);
    CAN0.setGeneralCallback(gotFrame);
    CAN0.watchFor();
    Serial.println(" CAN...............INIT");

    Serial.println(" all............inited");
    Serial.println("RESTAPI........done");

    delay(2000);
    restoreDisplay(*display, preferences);
}

#define SYNC_INTERVAL_MS 1000
static uint32_t last_sync = 0;

void loop()
{
    serialCommands.ReadSerial();
    ElegantOTA.loop();
    g_wsLink.loop();   // flush the batched WireProto stream to WS clients (single task)

    if (btMode == "ams")
    {
        Bluetooth::Service();

        // Detect BT disconnect and notify display to freeze content.
        static bool _prevBtConnected = false;
        bool _curBtConnected = Bluetooth::IsConnected();
        if (_prevBtConnected && !_curBtConnected)
            display->onBtDisconnected();
        _prevBtConnected = _curBtConnected;

        display->tickMedia();

        // Auto-time: sync display clock once per BT connection via CTS
        if (_autoTime && Bluetooth::IsConnected() && Bluetooth::IsTimeSet() && !_timeSyncDone)
        {
            struct tm t;
            if (getLocalTime(&t))
            {
                char buf[5];
                snprintf(buf, sizeof(buf), "%02d%02d", t.tm_hour, t.tm_min);
                display->setTime(buf);
                _timeSyncDone = true;
                Serial.printf("[BT] Auto-time synced: %s\n", buf);
            }
        }
        if (!Bluetooth::IsConnected())
            _timeSyncDone = false;  // reset so we sync again on next connect
    }

    WiFiManager::Handle(); // pump captive DNS while in AP fallback mode

    // Heap watchdog — BLE+WiFi+HTTP+AMS are tight on a classic ESP32. Log free,
    // all-time-min, and largest contiguous block (fragmentation) every 10s so we
    // can see headroom and cut load if it runs low.
    static uint32_t lastHeapLog = 0;
    if (millis() - lastHeapLog > 10000)
    {
        lastHeapLog = millis();
        Serial.printf("[heap] free=%u min=%u maxblk=%u\n",
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap(),
                      (unsigned)ESP.getMaxAllocHeap());
    }

    display->processEvents();
    uint32_t now = millis();
    if (now - last_sync > SYNC_INTERVAL_MS)
    {
        last_sync = now;
        display->tick();
    }

    // ELM327 (OBD) — currently disabled by default (elm_enabled=false) as the
    // V-LINK adapter is unreliable. When enabled it expects its own WiFi network;
    // reconciling that with home-WiFi STA is a separate task.
    if (_elmEnabled && WiFi.status() == WL_CONNECTED && elmManager)
    {
        elmManager->tick();
        float v;
        if (elmManager->getCached("PR071",     v)) display->onElmUpdate("PR071",     v);
        if (elmManager->getCached("DRV_BOOST",  v)) display->onElmUpdate("DRV_BOOST",  v);
    }
}
