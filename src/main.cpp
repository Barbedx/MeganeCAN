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

    bool ok = false;
    try
    {
        ok = fn();
    }
    catch (...)
    {
        Serial.printf("[BOOT] EXCEPTION in %s\n", name);
        ok = false;
    }

    Serial.printf("[BOOT] END   %-28s | ok=%s | dt=%lu ms\n",
                  name, ok ? "true" : "false", millis() - t0);
    logStage((String("POST  ") + name).c_str());
    return ok;
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

    runStage("WiFi.mode", []
             {
    WiFi.mode(WIFI_AP_STA);
    return true; });

    runStage("WiFi.setSleep", []
             {
    WiFi.setSleep(true);
    return true; });

    runStage("g_a2dp.begin", []
             {
                 g_a2dp.begin("MeganeCAN-A2DP");
                 return true; // replace with real return if available
             });

    runStage("WiFi.softAP", []
             {
    bool ok = WiFi.softAP(ssid, password);
    Serial.printf("[WiFi] softAP result=%d ip=%s\n", ok, WiFi.softAPIP().toString().c_str());
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
    WiFi.mode(WIFI_AP); //wifiapsta 
    WiFi.setSleep(true); // required when BT and WiFi coexist on same radio

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

#define SYNC_INTERVAL_MS 1000
static uint32_t last_sync = 0;

void loop()
{
    g_console.tick();
    

    // advance display tx state machine
    g_app.display->serviceTx(); //TODO implement media and events
 
    CanUtils::tick();

  //  g_app.display->tickMedia(); // TODO: Implement and move to ProcessEvents! or better TICKS! 
    
    
  ///  Serial.println("LOOP AFTEER DISPLAY TICK MEDIA........done");

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

  //  Serial.println("LOOP BEFORE DISPLAY processEvents ........done");
  //  g_app.display->processEvents();
  //  Serial.println("LOOP AFTEER DISPLAY processEvents........done");
    uint32_t now = millis();
    if (now - last_sync > SYNC_INTERVAL_MS)
    {
        last_sync = now;
   // Serial.println("LOOP BEFORE DISPLAY  g_app.display->tick(); ........done");
   //     g_app.display->tick();
   // Serial.println("LOOP AFTEER DISPLAY  g_app.display->tick();........done");
    }

   // ElmWifiManager::tick();
/*
    static uint32_t lastDiag = 0;
    if (millis() - lastDiag >= 2000) {
        lastDiag = millis();
        Serial.printf("[HEAP] free=%u min=%u largest=%u int=%u\n",
            ESP.getFreeHeap(),
            ESP.getMinFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }
    */
}
