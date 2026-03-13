#include "connectivity/ElmWifiManager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app/AppContext.h"

const uint32_t MyELMManager::kBackoffMaxMs = 30000;
extern AppContext g_app;
namespace
{
    IPAddress ELM_STA_IP(192, 168, 0, 151);
    IPAddress ELM_GATEWAY(192, 168, 0, 10);
    IPAddress ELM_SUBNET(255, 255, 255, 0);

    bool wifiBeginIssued = false;
    uint32_t lastWiFiAttemptMs = 0;
    const uint32_t WIFI_RETRY_MS = 5000;

    void connectToElm()
    {
        Serial.println("Configuring ELM STA...");

        WiFi.mode(WIFI_AP_STA);
        WiFi.persistent(false);
        WiFi.setAutoReconnect(true);
        WiFi.setSleep(true);

        WiFi.config(ELM_STA_IP, ELM_GATEWAY, ELM_SUBNET);
        Serial.println("Connecting to ELM WiFi (STA, static IP)...");
        WiFi.begin("V-LINK");

        wifiBeginIssued = true;
        lastWiFiAttemptMs = millis();
    }
}

namespace ElmWifiManager
{
    void tick()
    {
        if (!wifiBeginIssued)
        {
            if (g_app.elmEnabled)
                connectToElm();
        }
        else if (WiFi.status() != WL_CONNECTED)
        {
            static bool elmWifiLostLogged = false;
            if (!elmWifiLostLogged)
            {
                Serial.println("[ELM] Waiting for WiFi...");
                elmWifiLostLogged = true;
            }
        }
        else
        {
            static bool elmWifiConnLogged = false;
            if (!elmWifiConnLogged)
            {
                Serial.println("[ELM] Connected to ELM WiFi");
                elmWifiConnLogged = true;
            }
        }

        if (WiFi.status() == WL_CONNECTED && g_app.elmManager)
        {
            g_app.elmManager->tick();

            float v;
            if (g_app.elmManager->getCached("PR071", v))
                g_app.display->onElmUpdate("PR071", v);

            if (g_app.elmManager->getCached("DRV_BOOST", v))
                g_app.display->onElmUpdate("DRV_BOOST", v);
        }
    }
}