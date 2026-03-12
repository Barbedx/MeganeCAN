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
 
// ---- Static IP for V-LINK (STA) ----
// IPAddress ELM_STA_IP(192, 168, 0, 151); // choose a free IP (NOT 0.150)
// IPAddress ELM_GATEWAY(192, 168, 0, 10); // from your info
// IPAddress ELM_SUBNET(255, 255, 255, 0); // likely /24


// Enter your WIFI credentials in secret.h
const char *ssid = Soft_AP_WIFI_SSID;
const char *password = Soft_AP_WIFI_PASS;

// const char *ELM_SSID = "V-LINK";

 

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
void setup()
{
    delay(2000);

    g_console.begin();
    BoardDiagnostics::printReport();
 
    DisplayBootstrap::init(); 

    g_app.display->setKeyHandler(HandleKey);  // always set — HandleKey is mode-aware

    // Set WiFi mode + sleep first — required before any radio use (BLE or AP).
    // On ESP32-C3 (single radio), BLE must start before softAP to avoid the AP
    // beacon traffic blocking NimBLE init. (BT branch confirmed: BLE before AP.)
    WiFi.mode(WIFI_AP_STA);
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
     
    DisplayBootstrap::restore();
}

#define SYNC_INTERVAL_MS 1000
static uint32_t last_sync = 0;


void loop()
{
        g_console.tick();
    

        g_app.display->tickMedia(); //TODO: Implement 

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

    g_app.display->processEvents();
    uint32_t now = millis();
    if (now - last_sync > SYNC_INTERVAL_MS)
    {
        last_sync = now;
        g_app.display->tick();
    }
    
    ElmWifiManager::tick();

}
