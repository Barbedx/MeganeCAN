#include "bluetooth.h"
#include "utils/Log.h"

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
        volatile bool NeedSetup = false; // phone connected, services not all started yet
        volatile bool Secured   = false; // link encrypted/bonded
        RTC_DATA_ATTR bool TimeSet = false;

        // Each Apple service is brought up independently and retried until it
        // appears (iOS exposes AMS/ANCS/CTS with slightly different timing).
        bool AmsUp = false, AncsUp = false, CtsUp = false;
        uint32_t ConnectMs = 0;                 // when the phone connected
        const uint32_t SETUP_DEADLINE_MS = 20000; // stop retrying missing services after this

        std::string PeerAddr;
        const char *StatusText = "Pair from iPhone";
        uint32_t    lastSetupAttempt = 0;

        bool startCTS(NimBLEClient *c)
        {
            CurrentTimeService::CurrentTime ct;
            if (!CurrentTimeService::StartTimeService(c, &ct))
                return false;
            if (ct.mYear >= 2020 && ct.mYear <= 2100)
            {
                timeval tv;
                tv.tv_sec  = ct.ToTimeT();
                tv.tv_usec = static_cast<long>(ct.mSecondsFraction * 1000000.0f);
                if (settimeofday(&tv, nullptr) == 0)
                    TimeSet = true;
            }
            return true;
        }

        class ServerCallbacks : public NimBLEServerCallbacks
        {
            void onConnect(NimBLEServer *s, NimBLEConnInfo &connInfo) override
            {
                Log::printf("[BT] Phone connected: %s\n", connInfo.getAddress().toString().c_str());
                Client     = s->getClient(connInfo); // client over the inbound connection
                PeerAddr   = connInfo.getAddress().toString();
                Secured    = connInfo.isEncrypted();
                NeedSetup  = true;
                Connected  = false;
                AmsUp = AncsUp = CtsUp = false;
                ConnectMs  = millis();
                StatusText = "Connecting...";
            }

            void onDisconnect(NimBLEServer *s, NimBLEConnInfo &connInfo, int reason) override
            {
                Log::printf("[BT] Phone disconnected: %s reason=%d\n",
                              connInfo.getAddress().toString().c_str(), reason);
                Connected  = false;
                NeedSetup  = false;
                Secured    = false;
                AmsUp = AncsUp = CtsUp = false;
                Client     = nullptr;
                PeerAddr.clear();
                StatusText = HasBond() ? "Waiting for phone" : "Pair from iPhone";
                // advertiseOnDisconnect(true) restarts advertising automatically.
            }

            void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
            {
                Log::printf("[BT] Auth complete: bonded=%d encrypted=%d authenticated=%d\n",
                              connInfo.isBonded(), connInfo.isEncrypted(), connInfo.isAuthenticated());
                Secured = connInfo.isEncrypted();
            }
        };

        ServerCallbacks serverCb;

        void startAdvertising(const std::string &name)
        {
            NimBLEAdvertising *adv = Server->getAdvertising();

            // The primary 31-byte packet can't hold flags(3) + name + the 128-bit
            // AMS solicitation(18) once the name is long (e.g. "MeganeCAN" -> 32 > 31),
            // which silently drops the solicitation and iOS never treats us as an AMS
            // accessory. So: NAME in the primary packet (iOS Settings shows it), and
            // the AMS *solicitation* (AD 0x15) in the SCAN RESPONSE — iOS reads
            // solicited-service UUIDs from the combined active-scan data. addData()
            // has no length byte, so build [0x11][0x15][16 UUID LE].
            NimBLEAdvertisementData advData;
            advData.setFlags(0x06);
            advData.setName(name);
            adv->setAdvertisementData(advData);

            NimBLEAdvertisementData scanData;
            NimBLEUUID ams(APPLE_MEDIA_SERVICE_UUID);
            uint8_t sol[18];
            sol[0] = 0x11;
            sol[1] = 0x15;
            memcpy(&sol[2], ams.getValue(), 16);
            scanData.addData(sol, sizeof(sol));
            adv->setScanResponseData(scanData);
            adv->enableScanResponse(true);

            adv->start();
            Log::printf("[BT] Advertising '%s' (adv=%u, scanrsp=%u bytes) — pair from iPhone Settings",
                        name.c_str(), (unsigned)advData.getPayload().size(),
                        (unsigned)scanData.getPayload().size());
        }
    } // anonymous namespace

    void Begin(const std::string &device_name)
    {
        Log::printf("[BT] Begin() AMS/ANCS peripheral");
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
        Log::printf("[BT] Begin finished (bonds stored: %d)\n", NimBLEDevice::getNumBonds());
    }

    void Service()
    {
        // Drive deferred ANCS Control Point writes from this (loop) task.
        if (Connected && Client && Client->isConnected())
            AppleNotificationService::Process();

        // While no phone is connected, keep advertising so a bonded phone can
        // reconnect — iOS typically drops the link right after the first bond and
        // only exposes AMS/ANCS on the bonded reconnect.
        if (!(Client && Client->isConnected()))
        {
            static uint32_t lastAdv = 0;
            if (millis() - lastAdv > 3000)
            {
                lastAdv = millis();
                NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
                if (adv && !adv->isAdvertising())
                {
                    adv->start();
                    Log::printf("[BT] re-advertising (waiting for phone)");
                }
            }
            return;
        }

        // Connected: keep retrying service bring-up until everything is up.
        if (!NeedSetup)
            return;

        if (millis() - lastSetupAttempt < 800)
            return;
        lastSetupAttempt = millis();

        // Secure first, then discover (order matters — NimBLE issue #1033).
        if (!Secured)
        {
            Log::printf("[BT] Securing link — accept the pairing prompt on your iPhone...");
            if (!Client->secureConnection())
            {
                Log::printf("[BT] Pairing not complete yet, will retry");
                return;
            }
            Secured = true;
        }

        // Bring up each service independently and keep retrying the missing ones
        // (iOS exposes AMS/ANCS/CTS with slightly different timing).
        if (!AmsUp && AppleMediaService::StartMediaService(Client))
        {
            AmsUp = true;
            Connected = true;
            StatusText = "Connected";
            Log::printf("[BT] AMS started");
        }
        if (AmsUp && !AncsUp && AppleNotificationService::StartNotificationService(Client))
        {
            AncsUp = true;
            Log::printf("[BT] ANCS started");
        }
        if (AmsUp && !CtsUp && startCTS(Client))
        {
            CtsUp = true;
            Log::printf("[BT] CTS started");
        }

        // Done once everything is up, or give up on stragglers after the deadline.
        if ((AmsUp && AncsUp && CtsUp) || (millis() - ConnectMs > SETUP_DEADLINE_MS))
            NeedSetup = false;
    }

    bool IsConnected() { return Connected && Client && Client->isConnected(); }
    bool IsTimeSet()   { return TimeSet; }
    bool HasBond()     { return NimBLEDevice::getNumBonds() > 0; }

    void ClearBonds()
    {
        Log::printf("[BT] Clearing all bonds...");
        NimBLEDevice::deleteAllBonds();
        if (Client && Client->isConnected())
            Client->disconnect();
        Connected  = false;
        NeedSetup  = false;
        Secured    = false;
        StatusText = "Pair from iPhone";
        Log::printf("[BT] Bonds cleared. Also 'Forget this device' on the iPhone, then re-pair.");
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
