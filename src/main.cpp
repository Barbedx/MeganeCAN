#include <Arduino.h>
#include <time.h>

#include "server/HttpServerManager.h"
#include <secrets.h>
#include <WiFi.h>
// #include <ElegantOTA.h>

// #include <PsychicHttp.h>

#include "ElmManager/MyELMManager.h"

#include "app/AppContext.h"
#include "bluetooth/A2dpManager.h"
#include "app/SerialConsole.h"
#include "app/BoardDiagnostics.h"
#include "display/DisplayBootstrap.h"
#include "connectivity/ElmWifiManager.h"

AppContext g_app;
A2dpManager g_a2dp;
SerialConsole g_console;
static bool g_canReady = false;
static uint32_t g_canReadyAt = 0;
static bool g_softApActive = false;
static bool g_lastBtConnectionActive = false;
static constexpr bool kKeepSoftApDuringBt = true;
// ---- Static IP for V-LINK (STA) ----
// IPAddress ELM_STA_IP(192, 168, 0, 151); // choose a free IP (NOT 0.150)
// IPAddress ELM_GATEWAY(192, 168, 0, 10); // from your info
// IPAddress ELM_SUBNET(255, 255, 255, 0); // likely /24

// Enter your WIFI credentials in secret.h
const char *ssid = Soft_AP_WIFI_SSID;
const char *password = Soft_AP_WIFI_PASS;

// const char *ELM_SSID = "V-LINK";

static void logStage(const char *stage)
{
    Serial.printf(
        "[BOOT] %-28s | heap=%u min=%u largest=%u | free_int=%u\n",
        stage,
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

static bool runStage(const char *name, std::function<bool()> fn)
{
    uint32_t t0 = millis();
    logStage((String("BEGIN ") + name).c_str());

    bool ok = fn();

    Serial.printf("[BOOT] END   %-28s | ok=%s | dt=%lu ms\n",
                  name, ok ? "true" : "false", millis() - t0);
    logStage((String("POST  ") + name).c_str());
    return ok;
}

static bool ensureSoftApState(bool shouldBeActive)
{
    if (g_app.elmEnabled)
        return true;

    if (g_softApActive == shouldBeActive)
        return true;

    if (shouldBeActive)
    {
        Serial.println("[WiFi] Restoring SoftAP because BT is disconnected");
        bool ok = WiFi.softAP(ssid, password);
        Serial.printf("[WiFi] softAP result=%d ip=%s\n", ok, WiFi.softAPIP().toString().c_str());
        if (ok)
            g_softApActive = true;
        return ok;
    }

    Serial.println("[WiFi] Disabling SoftAP because BT is connected");
    bool ok = WiFi.softAPdisconnect(true);
    g_softApActive = false;
    if (!ok)
        Serial.println("[WiFi] softAPdisconnect reported false; treating AP as disabled for coexistence");
    return ok;
}

static const char *wifiModeName(wifi_mode_t mode)
{
    switch (mode)
    {
    case WIFI_OFF:
        return "OFF";
    case WIFI_STA:
        return "STA";
    case WIFI_AP:
        return "AP";
    case WIFI_AP_STA:
        return "AP_STA";
    default:
        return "UNKNOWN";
    }
}

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_AP_START:
        Serial.println("[WiFiEvt] AP_START");
        break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
        Serial.println("[WiFiEvt] AP_STOP");
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        Serial.printf("[WiFiEvt] AP_STACONNECTED aid=%u\n", info.wifi_ap_staconnected.aid);
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        Serial.printf("[WiFiEvt] AP_STADISCONNECTED aid=%u\n", info.wifi_ap_stadisconnected.aid);
        break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        Serial.println("[WiFiEvt] AP_STAIPASSIGNED");
        break;
    default:
        break;
    }
}

void gotFrame(CAN_FRAME *frame)
{
    if (frame->id != 0x3CF && frame->id != 0x3AF && frame->id != 0x7AF)
        CanUtils::printCanFrame(*frame, false);
    g_app.display->recv(frame);
}

bool HandleKey(AffaCommon::AffaKey key, bool isHold)
{
    (void)isHold;

    switch (key)
    {
    case AffaCommon::AffaKey::Pause:
        g_a2dp.playPause();
        break;

    case AffaCommon::AffaKey::RollUp:
        g_a2dp.next();
        break;

    case AffaCommon::AffaKey::RollDown:
        g_a2dp.previous();
        break;

    default:
        break;
    }

    return true;
}
void setup() // debug
{
    runStage("console.begin", []
             {
    g_console.begin();
    return true; });

    runStage("BoardDiagnostics::printReport", []
             {
    BoardDiagnostics::printReport();
    return true; });

    runStage("DisplayBootstrap::init", []
             {
    DisplayBootstrap::init();
    return g_app.display != nullptr; });

    runStage("display->setKeyHandler", []
             {
    g_app.display->setKeyHandler(HandleKey);
    return true; });

    runStage("WiFi.onEvent", []
             {
    WiFi.onEvent(onWiFiEvent);
    return true; });

    runStage("WiFi.mode", []
             {
    wifi_mode_t mode = g_app.elmEnabled ? WIFI_STA : WIFI_AP;
    Serial.printf("[WiFi] Selecting mode: %s\n", mode == WIFI_STA ? "WIFI_STA" : "WIFI_AP");
    WiFi.mode(mode);
    return true; });

    runStage("WiFi.setSleep", []
             {
    bool sleepEnabled = g_app.elmEnabled;
    Serial.printf("[WiFi] Sleep: %s\n", sleepEnabled ? "on" : "off");
    WiFi.setSleep(sleepEnabled);
    return true; });

    runStage("g_a2dp.begin", []
             {
                g_a2dp.begin("MeganeCAN-A2DP");
                 return true; // replace with real return if available
             });

    runStage("WiFi.softAP", []
             {
    if (g_app.elmEnabled)
    {
        Serial.println("[WiFi] SoftAP skipped in STA/ELM mode");
        g_softApActive = false;
        return true;
    }
    bool ok = WiFi.softAP(ssid, password);
    Serial.printf("[WiFi] softAP result=%d ip=%s\n", ok, WiFi.softAPIP().toString().c_str());
    g_softApActive = ok;
    return ok; });

    runStage("serverManager->begin", []
             {
   g_app.serverManager->begin();
    return true; });

    runStage("display->begin", []
             {
    g_app.display->begin();
    return true; });
    
    runStage("CAN init", [] {
        
        CanUtils::begin();
        CAN0.setCANPins(GPIO_NUM_3, GPIO_NUM_4);
        CAN0.begin(CAN_BPS_500K);
        CAN0.setGeneralCallback(gotFrame);
        CAN0.watchFor();
        CanUtils::setReady(true);
        
        Serial.println("CAN...............INIT");
        return true;
    });
    
    delay(2000);
}

void setupOld()
{
    delay(2000);

    g_console.begin();
    BoardDiagnostics::printReport();

    DisplayBootstrap::init();

    g_app.display->setKeyHandler(HandleKey); // always set — HandleKey is mode-aware

    // Set WiFi mode + sleep first — required before any radio use (BLE or AP).
    // On ESP32-C3 (single radio), BLE must start before softAP to avoid the AP
    // beacon traffic blocking NimBLE init. (BT branch confirmed: BLE before AP.)
    WiFi.mode(g_app.elmEnabled ? WIFI_STA : WIFI_AP);
    WiFi.setSleep(g_app.elmEnabled);

    Serial.println();
    Serial.println(F("ESP32 BOARD INSPECTOR + A2DP SINK"));
    Serial.println(F("Pair your phone with: MeganeCAN-A2DP"));
    g_a2dp.begin("MeganeCAN-A2DP");

    // Start AP after BLE task is queued — coexistence is established by the time
    // the AP begins beaconing.
    WiFi.softAP(ssid, password);
    Serial.print("[WiFi] AP started: ");
    Serial.print(ssid);
    Serial.print(" @ ");
    Serial.println(WiFi.softAPIP());
    g_app.serverManager->begin();

    g_app.display->begin();

    CAN0.setCANPins(GPIO_NUM_3, GPIO_NUM_4);
    CAN0.begin(CAN_BPS_500K);
    CAN0.setGeneralCallback(gotFrame);
    CAN0.watchFor();
    Serial.println(" CAN...............INIT");

    Serial.println(" all............inited");
    Serial.println("RESTAPI........done");

    delay(2000);

    // DisplayBootstrap::restore();
}

void loop()
{
    g_console.tick();
    CanUtils::tick();
    g_a2dp.tick();

    if (!kKeepSoftApDuringBt)
    {
        bool btConnectionActive = g_a2dp.isConnectionActive();
        if (btConnectionActive != g_lastBtConnectionActive)
        {
            ensureSoftApState(!btConnectionActive);
            g_lastBtConnectionActive = btConnectionActive;
        }
    }

    if (g_app.display)
    {
        g_app.display->setMediaInfo(g_a2dp.trackInfo());
        g_app.display->tick();
    }

    // Auto-time: sync display clock once per BT connection via CTS
    /*   if (_autoTime && Bluetooth::IsConnected() && Bluetooth::IsTimeSet() && !_timeSyncDone)
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
          */
    //}

   // ElmWifiManager::tick();
    static uint32_t lastDiag = 0;
    if (millis() - lastDiag >= 5000) {
        lastDiag = millis();
        Serial.printf("[COEX] wifi=%s apClients=%d bt=%s audio=%s play=%s free=%u min=%u largest=%u int=%u\n",
            wifiModeName(WiFi.getMode()),
            WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA ? WiFi.softAPgetStationNum() : 0,
            g_a2dp.connectionStateName(),
            g_a2dp.audioStateName(),
            g_a2dp.playbackStatusName(),
            ESP.getFreeHeap(),
            ESP.getMinFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }

    // Arduino-as-component on ESP-IDF does not implicitly yield on dual-core,
    // so give IDLE1 a chance to run and service the task watchdog.
    delay(1);
}
