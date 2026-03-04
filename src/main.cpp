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
#include "display/Affa2/Affa2Display.h"
#include "display/Affa2/Affa2MenuDisplay.h"
#include "display/Affa2/Affa2Base.h"
#include "display/Affa3Nav/Affa3NavDisplay.h"
#include "bluetooth.h"
#include "apple_media_service.h"
#include "BleMediaKeyboard.h"

AffaDisplayBase *display = nullptr;
unsigned long lastPingTime = 0;

// BT mode state (read from NVS in initDisplay, used throughout)
String btMode = "ams";   // "ams" or "keyboard"
bool _autoTime = true;   // sync display clock from CTS (AMS mode only)
bool _timeSyncDone = false; // reset each time BT disconnects
bool _elmEnabled = false; // ELM327 enabled (read from NVS, configurable via Web UI)

BleMediaKeyboard bleKeyboard;
// ---- Static IP for V-LINK (STA) ----
IPAddress ELM_STA_IP(192, 168, 0, 151); // choose a free IP (NOT 0.150)
IPAddress ELM_GATEWAY(192, 168, 0, 10); // from your info
IPAddress ELM_SUBNET(255, 255, 255, 0); // likely /24
Preferences preferences;
// HttpServerManager serverManager(*display, preferences);
HttpServerManager *serverManager = nullptr;
MyELMManager *elmManager = nullptr;

// Enter your WIFI credentials in secret.h
const char *ssid = Soft_AP_WIFI_SSID;
const char *password = Soft_AP_WIFI_PASS;

const char *ELM_SSID = "V-LINK";

void gotFrame(CAN_FRAME *frame)
{
    if (frame->id != 0x3CF && frame->id != 0x3AF && frame->id != 0x7AF)
        CanUtils::printCanFrame(*frame, false);
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

// Create command objects
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
    Preferences prefs;
    prefs.begin("config", true);
    String displayType = prefs.getString("display_type", "affa3nav");
    btMode      = prefs.getString("bt_mode",     "ams");
    _autoTime   = prefs.getBool("auto_time",     true);
    _elmEnabled = prefs.getBool("elm_enabled", false);
    prefs.end();

    Serial.println("[Display Init] Display type: " + displayType);
    Serial.println("[Display Init] BT mode: " + btMode);
    Serial.println("[Display Init] Auto-time: " + String(_autoTime ? "on" : "off"));
    Serial.println("[Display Init] ELM enabled: " + String(_elmEnabled ? "yes" : "no"));

    if (displayType == "affa3nav")
    {
        Serial.println("[Display Init] Instantiating Affa3NavDisplay");
        display = new Affa3NavDisplay();
    }
    else if (displayType == "affa2")
    {
        Serial.println("[Display Init] Instantiating Affa2Display (8-segment)");
        display = new Affa2Display();
    }
    else if (displayType == "affa2menu")
    {
        Serial.println("[Display Init] Instantiating Affa2MenuDisplay (full LED)");
        display = new Affa2MenuDisplay();
    }
    else
    {
        Serial.println("[Display Init] Instantiating Affa3Display (default)");
        display = new Affa2Base();
    }

    // NOTE: display->begin() is called in setup() AFTER BT mode configuration
    elmManager = new MyELMManager(*display);
    serverManager = new HttpServerManager(*display, preferences);
    serverManager->attachElm(elmManager);
    elmManager->loadHeaderConfig(preferences); // load per-header enable/disable from NVS
    Serial.println("[Display Init] HttpServerManager initialized");
}
static bool wifiBeginIssued = false;

static uint32_t lastWiFiAttemptMs = 0;
static const uint32_t WIFI_RETRY_MS = 5000; // retry every 5s if not connected

// One-time connect helper — keeps AP alive (WIFI_AP_STA)
void connectToElm()
{
    Serial.println("Configuring ELM STA...");

    // Keep mode as AP_STA so the web UI stays reachable even while connecting to V-LINK
    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(true); // required when both WiFi and BT are active

    WiFi.config(ELM_STA_IP, ELM_GATEWAY, ELM_SUBNET); // static IP for V-LINK
    Serial.println("Connecting to ELM WiFi (STA, static IP)...");
    WiFi.begin("V-LINK"); // open network (no password)

    wifiBeginIssued = true;
    lastWiFiAttemptMs = millis();
}

bool HandleKey(AffaCommon::AffaKey key, bool isHold)
{
    if (btMode == "ams")
    {
        if (!Bluetooth::IsConnected())
        {
            // Use steering wheel keys to cycle through discovered AMS devices
            switch (key)
            {
            case AffaCommon::AffaKey::RollUp:   Bluetooth::SelectNext(); break;
            case AffaCommon::AffaKey::RollDown:  Bluetooth::SelectPrev(); break;
            case AffaCommon::AffaKey::Pause:     Bluetooth::ConnectSelected(); break;
            default: break;
            }
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
    info.dump();
    display->setMediaInfo(info);
    g_mediaInfo = info;
}
void setup()
{
    delay(2000);
    initDisplay();
    initSerial();

    display->setKeyHandler(HandleKey);  // always set — HandleKey is mode-aware

    // Set WiFi mode + sleep first — required before any radio use (BLE or AP).
    // On ESP32-C3 (single radio), BLE must start before softAP to avoid the AP
    // beacon traffic blocking NimBLE init. (BT branch confirmed: BLE before AP.)
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(true); // required when BT and WiFi coexist on same radio

    if (btMode == "ams")
    {
        AppleMediaService::RegisterForNotifications(
            onDataUpdateCallback,
            AppleMediaService::NotificationLevel::All);
        // Launch BLE init BEFORE softAP so the radio is free during NimBLE startup.
        xTaskCreate([](void*) {
            Bluetooth::Begin("MeganeCAN");
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

    // Start AP after BLE task is queued — coexistence is established by the time
    // the AP begins beaconing.
    WiFi.softAP(ssid, password);
    Serial.print("[WiFi] AP started: ");
    Serial.print(ssid);
    Serial.print(" @ ");
    Serial.println(WiFi.softAPIP());
    serverManager->begin();

    display->begin();

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

// Helper to print human-readable WiFi status
const char *wlStr(wl_status_t s)
{
    switch (s)
    {
    case WL_IDLE_STATUS:
        return "IDLE";
    case WL_NO_SSID_AVAIL:
        return "NO_SSID";
    case WL_SCAN_COMPLETED:
        return "SCAN_DONE";
    case WL_CONNECTED:
        return "CONNECTED";
    case WL_CONNECT_FAILED:
        return "FAILED";
    case WL_CONNECTION_LOST:
        return "LOST";
    case WL_DISCONNECTED:
        return "DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}
void loop()
{
    serialCommands.ReadSerial();
    ElegantOTA.loop();

    if (btMode == "ams")
    {
        Bluetooth::Service();
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

    display->processEvents();
    uint32_t now = millis();
    if (now - last_sync > SYNC_INTERVAL_MS)
    {
        last_sync = now;

    }
    // 1) Start connecting ONCE (only when ELM is enabled via Web UI)
    if (!wifiBeginIssued)
    {
        if (_elmEnabled)
            connectToElm();
    }
    else if (WiFi.status() != WL_CONNECTED)
    {
        // Not connected yet — nothing to do, just wait.
        static bool _elmWifiLostLogged = false;
        if (!_elmWifiLostLogged) {
            Serial.println("[ELM] Waiting for WiFi...");
            _elmWifiLostLogged = true;
        }
    }
    else
    {
        static bool _elmWifiConnLogged = false;
        if (!_elmWifiConnLogged) {
            Serial.println("[ELM] Connected to ELM WiFi");
            _elmWifiConnLogged = true;
        }
    }

    // 2) When Wi-Fi is up, let the ELM manager build/maintain TCP+session
    if (WiFi.status() == WL_CONNECTED && elmManager)
    {
        elmManager->tick();

        // Push ELM values to display (updates menu fields in-place)
        float v;
        if (elmManager->getCached("PR071",    v)) display->onElmUpdate("PR071",    v);
        if (elmManager->getCached("DRV_BOOST", v)) display->onElmUpdate("DRV_BOOST", v);
    }

}
