#include <Arduino.h>
#include "bluetooth.h"
#include <NimBLEDevice.h>

#include <time.h>
#include <sys/time.h>

#include "apple_media_service.h"
#include "current_time_service.h"

#define APPLE_MUSIC_SERVICE_UUID "89D3502B-0F36-433A-8EF4-C502AD55F8DC"

#define ANCS_SERVICE_UUID "7905F431-B5CE-4E99-A40F-4B1E122D00D0"

namespace Bluetooth
{
    namespace
    {
        // --- State ---
        bool Ended     = true;
        bool Connected = false;

        BLEClient  *Client     = nullptr;
        BLEAddress  TargetAddr;
        bool        TargetFound = false;

        RTC_DATA_ATTR bool TimeSet = false;

        // --- Scan callback: look for Apple device advertising Spotify UUID ---
        class AmsScanCallbacks : public NimBLEScanCallbacks
        {
        public:
            void onDiscovered(const NimBLEAdvertisedDevice *dev) override {}

            void onResult(const NimBLEAdvertisedDevice *dev) override
            {
                if (TargetFound) return;
                if (!dev->isConnectable()) return;

                const std::string &mfg = dev->getManufacturerData();
                if (mfg.size() < 2) return;

                // Apple devices only (company ID 0x004C)
                uint16_t company = (uint8_t)mfg[0] | ((uint8_t)mfg[1] << 8);
                if (company != 0x004C) return;

                // Must be advertising Spotify UUID — guarantees it's the user's phone
                // with Spotify open (not a neighbour's device)
                static const NimBLEUUID kSpotifyUUID("3e1d50cd-7e3e-427d-8e1c-b78aa87fe624");
                if (!dev->haveServiceUUID() || !dev->isAdvertisingService(kSpotifyUUID))
                    return;

                Serial.printf("[BT] Spotify UUID found on %s — connecting\n",
                              dev->getAddress().toString().c_str());

                TargetAddr  = dev->getAddress();
                TargetFound = true;
                NimBLEDevice::getScan()->stop();
            }

            void onScanEnd(const NimBLEScanResults &results, int reason) override
            {
                Serial.printf("[BT] Scan ended, %d devices seen, reason=%d\n",
                              results.getCount(), reason);
            }
        };

        bool ConnectToAms()
        {
            if (!TargetFound)
            {
                Serial.println("[BT] ConnectToAms: no target yet");
                return false;
            }

            if (Client)
            {
                NimBLEDevice::deleteClient(Client);
                Client = nullptr;
            }

            Serial.printf("[BT] Connecting to %s\n", TargetAddr.toString().c_str());

            Client = NimBLEDevice::createClient();
            if (!Client)
            {
                Serial.println("[BT] Failed to create client");
                return false;
            }

            if (!Client->connect(TargetAddr))
            {
                Serial.println("[BT] connect() failed — restarting scan");
                NimBLEDevice::deleteClient(Client);
                Client      = nullptr;
                Connected   = false;
                TargetFound = false;
                NimBLEScan *s = NimBLEDevice::getScan();
                if (s) s->start(0, false, false);
                return false;
            }

            Serial.println("[BT] Connected, securing...");

            if (!Client->secureConnection())
            {
                Serial.println("[BT] secureConnection() failed — restarting scan");
                Client->disconnect();
                NimBLEDevice::deleteClient(Client);
                Client      = nullptr;
                Connected   = false;
                TargetFound = false;
                NimBLEScan *s = NimBLEDevice::getScan();
                if (s) s->start(0, false, false);
                return false;
            }

            if (!AppleMediaService::StartMediaService(Client))
            {
                Serial.println("[BT] StartMediaService failed — restarting scan");
                Client->disconnect();
                NimBLEDevice::deleteClient(Client);
                Client      = nullptr;
                Connected   = false;
                TargetFound = false;
                NimBLEScan *s = NimBLEDevice::getScan();
                if (s) s->start(0, false, false);
                return false;
            }

            Serial.println("[BT] AMS started");

            CurrentTimeService::CurrentTime time;
            if (!CurrentTimeService::StartTimeService(Client, &time))
            {
                Serial.println("[BT] StartTimeService failed");
            }
            else
            {
                time_t unix = time.ToTimeT();
                if (time.mYear >= 2020 && time.mYear <= 2100)
                {
                    timeval new_time;
                    new_time.tv_sec  = unix;
                    new_time.tv_usec = (long)(time.mSecondsFraction * 1000000.0f);
                    if (settimeofday(&new_time, nullptr) == 0)
                        TimeSet = true;
                    time.Dump();
                }
                else
                {
                    Serial.printf("[BT] CTS invalid year=%u — ignoring\n", time.mYear);
                }
            }

            Connected = true;
            Serial.println("[BT] ConnectToAms: success");
            return true;
        }

    } // anonymous namespace

    // ------------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------------

    void ClearBonds()
    {
        Serial.println("[BT] Clearing all BLE bonds...");
        NimBLEDevice::deleteAllBonds();

        if (Client)
        {
            if (Client->isConnected()) Client->disconnect();
            NimBLEDevice::deleteClient(Client);
            Client = nullptr;
        }

        Connected   = false;
        TargetFound = false;
        TimeSet     = false;

        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan)
        {
            Serial.println("[BT] Restarting scan after clearbonds...");
            scan->start(0, false, false);
        }
        Serial.println("[BT] Bonds cleared. On iPhone: Settings → Bluetooth → Forget MeganeCAN, then open Spotify.");
    }

    void Begin(const std::string &device_name)
    {
        Serial.println("[BT] Begin()");
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        Ended       = false;
        Connected   = false;
        TimeSet     = false;
        TargetFound = false;

        NimBLEDevice::init(device_name);

        // bonding=false: Just Works pairing each time (no stored IRK / resolving list).
        // This avoids BLE_HS_EPREEMPTED (err=13) which is triggered by the privacy
        // layer rebuilding the HCI resolving list when bonding=true.
        NimBLEDevice::setSecurityAuth(/*bonding=*/false, /*mitm=*/false, /*sc=*/true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

        NimBLEScan *scan = NimBLEDevice::getScan();
        static AmsScanCallbacks cb;
        scan->setScanCallbacks(&cb);
        scan->setActiveScan(true);
        scan->setInterval(45);
        scan->setWindow(30);
        scan->setDuplicateFilter(false);
        scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
        scan->start(0, false, false);

        Serial.println("[BT] Begin finished — open Spotify on your phone to connect");
    }

    void End()
    {
        Serial.println("[BT] End()");
        Ended       = true;
        Connected   = false;
        TargetFound = false;

        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan) scan->stop();

        if (Client)
        {
            Client->disconnect();
            NimBLEDevice::deleteClient(Client);
            Client = nullptr;
        }

        NimBLEDevice::deinit(true);
    }

    void Service()
    {
        if (Ended) return;

        // Client dropped → clean up and rescan
        if (Client && !Client->isConnected())
        {
            Serial.println("[BT] Client disconnected, restarting scan");
            NimBLEDevice::deleteClient(Client);
            Client      = nullptr;
            Connected   = false;
            TimeSet     = false;
            TargetFound = false;

            NimBLEScan *scan = NimBLEDevice::getScan();
            if (scan && !scan->isScanning())
                scan->start(0, false, false);
        }

        // Target found → connect
        if ((!Client || !Client->isConnected()) && TargetFound)
        {
            Serial.println("[BT] Target found, connecting...");
            ConnectToAms();
        }
    }

    bool  IsConnected() { return Connected && Client && Client->isConnected(); }
    bool IsTimeSet()   { return TimeSet; }

    // --- Web UI stubs (no candidate list in this simple mode) ---

    const char *GetStatusText()
    {
        if (Connected && Client && Client->isConnected()) return "Connected";
        if (TargetFound) return "Connecting...";
        return "Scanning (open Spotify)";
    }

    void SelectNext()     {}
    void SelectPrev()     {}
    void ConnectSelected(){}
    void SelectByIndex(int) {}

    void ForgetDevice()
    {
        NimBLEDevice::deleteAllBonds();
        if (Client)
        {
            if (Client->isConnected()) Client->disconnect();
            NimBLEDevice::deleteClient(Client);
            Client = nullptr;
        }
        Connected   = false;
        TargetFound = false;
        TimeSet     = false;
        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan && !scan->isScanning()) scan->start(0, false, false);
        Serial.println("[BT] ForgetDevice done — open Spotify to reconnect");
    }

    String GetStatusJson()
    {
        bool conn = IsConnected();
        String json = "{";
        json += "\"connected\":" + String(conn ? "true" : "false") + ",";
        json += "\"status\":\"" + String(GetStatusText()) + "\",";
        json += "\"candidates\":[]";
        json += "}";
        return json;
    }

} // namespace Bluetooth
