#include "bluetooth.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <time.h>
#include <sys/time.h>
#include <string.h>

#include "apple_media_service.h"
#include "apple_notification_service.h"
#include "current_time_service.h"

// Apple Media Service (hosted by the iPhone; we are its client).
#define APPLE_MEDIA_SERVICE_UUID "89D3502B-0F36-433A-8EF4-C502AD55F8DC"

namespace Bluetooth
{
    namespace
    {
        NimBLEServer *Server = nullptr;
        NimBLEClient *Client = nullptr; // GATT client bound to the phone-initiated connection

        volatile bool Connected = false; // AMS is up
        volatile bool NeedSetup = false; // phone connected, services not started yet
        volatile bool Secured   = false; // link encrypted/bonded
        RTC_DATA_ATTR bool TimeSet = false;

        std::string PeerAddr;
        const char *StatusText = "Pair from iPhone";
        uint32_t    lastSetupAttempt = 0;

        bool startServices(NimBLEClient *c)
        {
            if (!AppleMediaService::StartMediaService(c))
            {
                Serial.println("[BT] StartMediaService not ready yet (will retry)");
                return false;
            }
            Serial.println("[BT] AMS started");

            // ANCS (notifications) is optional — log and continue if absent.
            AppleNotificationService::StartNotificationService(c);

            CurrentTimeService::CurrentTime ct;
            if (CurrentTimeService::StartTimeService(c, &ct))
            {
                if (ct.mYear >= 2020 && ct.mYear <= 2100)
                {
                    timeval tv;
                    tv.tv_sec  = ct.ToTimeT();
                    tv.tv_usec = static_cast<long>(ct.mSecondsFraction * 1000000.0f);
                    if (settimeofday(&tv, nullptr) == 0)
                        TimeSet = true;
                }
            }
            else
            {
                Serial.println("[BT] StartTimeService failed (continuing)");
            }
            return true;
        }

        class ServerCallbacks : public NimBLEServerCallbacks
        {
            void onConnect(NimBLEServer *s, NimBLEConnInfo &connInfo) override
            {
                Serial.printf("[BT] Phone connected: %s\n", connInfo.getAddress().toString().c_str());
                Client     = s->getClient(connInfo); // client over the inbound connection
                PeerAddr   = connInfo.getAddress().toString();
                Secured    = connInfo.isEncrypted();
                NeedSetup  = true;
                Connected  = false;
                StatusText = "Connecting...";
            }

            void onDisconnect(NimBLEServer *s, NimBLEConnInfo &connInfo, int reason) override
            {
                Serial.printf("[BT] Phone disconnected: %s reason=%d\n",
                              connInfo.getAddress().toString().c_str(), reason);
                Connected  = false;
                NeedSetup  = false;
                Secured    = false;
                Client     = nullptr;
                PeerAddr.clear();
                StatusText = HasBond() ? "Waiting for phone" : "Pair from iPhone";
                // advertiseOnDisconnect(true) restarts advertising automatically.
            }

            void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
            {
                Serial.printf("[BT] Auth complete: bonded=%d encrypted=%d authenticated=%d\n",
                              connInfo.isBonded(), connInfo.isEncrypted(), connInfo.isAuthenticated());
                Secured = connInfo.isEncrypted();
            }
        };

        ServerCallbacks serverCb;

        void startAdvertising(const std::string &name)
        {
            NimBLEAdvertising *adv = Server->getAdvertising();

            // Name in the PRIMARY packet (iOS Settings hides scan-response-only names)
            // + AMS UUID as a *solicited* service (AD type 0x15, 128-bit). addData()
            // does not prepend the AD length byte, so build [0x11][0x15][16 UUID LE].
            // Once bonded iOS exposes both AMS and ANCS (NimBLE issue #1033).
            NimBLEAdvertisementData advData;
            advData.setFlags(0x06);
            advData.setName(name);

            NimBLEUUID ams(APPLE_MEDIA_SERVICE_UUID);
            uint8_t sol[18];
            sol[0] = 0x11;
            sol[1] = 0x15;
            memcpy(&sol[2], ams.getValue(), 16);
            advData.addData(sol, sizeof(sol));

            adv->setAdvertisementData(advData);
            adv->start();
            Serial.printf("[BT] Advertising as '%s' — pair from iPhone Settings > Bluetooth\n", name.c_str());
        }
    } // anonymous namespace

    void Begin(const std::string &device_name)
    {
        Serial.println("[BT] Begin() AMS/ANCS peripheral");
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        NimBLEDevice::init(device_name);
        NimBLEDevice::setMTU(247); // larger ATT MTU so ANCS message bodies fit

        // Bonding ON, no MITM, LE Secure Connections -> iOS "Just Works" pairing,
        // keys persisted in NVS so reconnection is silent.
        NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

        Server = NimBLEDevice::createServer();
        Server->setCallbacks(&serverCb);
        Server->advertiseOnDisconnect(true);
        startAdvertising(device_name);

        StatusText = HasBond() ? "Waiting for phone" : "Pair from iPhone";
        Serial.printf("[BT] Begin finished (bonds stored: %d)\n", NimBLEDevice::getNumBonds());
    }

    void Service()
    {
        // Drive deferred ANCS Control Point writes from this (loop) task.
        if (Connected && Client && Client->isConnected())
            AppleNotificationService::Process();

        if (!(NeedSetup && Client && Client->isConnected() && !Connected))
            return;

        if (millis() - lastSetupAttempt < 800)
            return;
        lastSetupAttempt = millis();

        // Secure first, then discover (order matters — NimBLE issue #1033).
        if (!Secured)
        {
            Serial.println("[BT] Securing link — accept the pairing prompt on your iPhone...");
            if (!Client->secureConnection())
            {
                Serial.println("[BT] Pairing not complete yet, will retry");
                return;
            }
            Secured = true;
        }

        if (startServices(Client))
        {
            Connected  = true;
            NeedSetup  = false;
            StatusText = "Connected";
            Serial.println("[BT] Connected — AMS + ANCS + CTS up");
        }
    }

    bool IsConnected() { return Connected && Client && Client->isConnected(); }
    bool IsTimeSet()   { return TimeSet; }
    bool HasBond()     { return NimBLEDevice::getNumBonds() > 0; }

    void ClearBonds()
    {
        Serial.println("[BT] Clearing all bonds...");
        NimBLEDevice::deleteAllBonds();
        if (Client && Client->isConnected())
            Client->disconnect();
        Connected  = false;
        NeedSetup  = false;
        Secured    = false;
        StatusText = "Pair from iPhone";
        Serial.println("[BT] Bonds cleared. Also 'Forget this device' on the iPhone, then re-pair.");
    }

    const char *GetStatusText() { return StatusText; }

    String GetStatusJson()
    {
        String j = "{";
        j += "\"connected\":";  j += (IsConnected() ? "true" : "false");
        j += ",\"status\":\"";  j += StatusText; j += "\"";
        j += ",\"bonded\":";    j += (HasBond() ? "true" : "false");
        j += ",\"address\":\""; j += (IsConnected() ? PeerAddr.c_str() : ""); j += "\"";
        j += "}";
        return j;
    }

} // namespace Bluetooth
