
#include <Arduino.h>
#include "bluetooth.h"
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <set>

#include <time.h>
#include <sys/time.h>

#if defined(CONFIG_NIMBLE_CPP_IDF)
#include <host/ble_gap.h>
#else
#include <nimble/nimble/host/include/host/ble_gap.h>
#endif
#include "apple_media_service.h"
#include "current_time_service.h"


#define APPLE_MUSIC_SERVICE_UUID "89D3502B-0F36-433A-8EF4-C502AD55F8DC"

#define ANCS_SERVICE_UUID "7905F431-B5CE-4E99-A40F-4B1E122D00D0"

#define DELSOL_VEHICLE_SERVICE_UUID "8fb88487-73cf-4cce-b495-505a4b54b802"
#define DELSOL_STATUS_CHARACTERISTIC_UUID "40d527f5-3204-44a2-a4ee-d8d3c16f970e"
#define DELSOL_BATTERY_CHARACTERISTIC_UUID "5c258bb8-91fc-43bb-8944-b83d0edc9b43"

#define DELSOL_LOCATION_SERVICE_UUID "61d33c70-e3cd-4b31-90d8-a6e14162fffd"
#define DELSOL_NAVIGATION_SERVICE_UUID "77f5d2b5-efa1-4d55-b14a-cc92b72708a0"

namespace Bluetooth
{
    // ------------------------------------------------------------------
    // State machine
    // ------------------------------------------------------------------
    // IDLE       → Begin() not yet done (NimBLE not initialised)
    // SCANNING   → scan active, collecting Apple-device candidates
    // CONNECTING → scan stopped, attempting connect to selected candidate
    // CONNECTED  → AMS running on Client
    // ------------------------------------------------------------------
    enum class BtState { IDLE, SCANNING, CONNECTING, CONNECTED };

    namespace
    {
        // --- Core state ---
        bool                 Ended = false;
        volatile BtState     state = BtState::IDLE;

        BLEClient           *Client     = nullptr;
        BLEAddress           TargetAddr {"00:00:00:00:00:00"};

        RTC_DATA_ATTR bool   TimeSet    = false;

        // --- Candidate list ---
        struct BtCandidate {
            NimBLEAddress addr;
            std::string   name;
            int8_t        rssi    = -127;
            uint8_t       mfgType = 0;
            BtCandidate(NimBLEAddress a, std::string n, int8_t r, uint8_t t)
                : addr(a), name(n), rssi(r), mfgType(t) {}
        };
        static std::vector<BtCandidate> candidates;
        static int                      selectedIdx   = 0;
        static std::string              preferredAddr;
        static std::set<std::string>    skippedDevices;
        static constexpr size_t         MAX_CANDIDATES = 8;

        // Auto-try timer (lives here so it can be reset on reconnect)
        static uint32_t lastAutoTryMs = 0;

        // --- NVS helpers ---
        static void SavePreferredAddr(const std::string &addr)
        {
            Preferences prefs;
            prefs.begin("bluetooth", false);
            prefs.putString("pref_addr", addr.c_str());
            prefs.end();
            Serial.printf("[BT] Saved preferred addr: %s\n", addr.c_str());
        }

        static std::string LoadPreferredAddr()
        {
            Preferences prefs;
            prefs.begin("bluetooth", true);
            String s = prefs.getString("pref_addr", "");
            prefs.end();
            return s.c_str();
        }

        static void ClearPreferredAddr()
        {
            Preferences prefs;
            prefs.begin("bluetooth", false);
            prefs.remove("pref_addr");
            prefs.end();
            preferredAddr.clear();
            Serial.println("[BT] Preferred addr cleared");
        }

        // --- Returns a short display label for a candidate ---
        static std::string CandidateLabel(const BtCandidate &c)
        {
            if (!c.name.empty())
                return c.name;
            std::string a = c.addr.toString();
            return a.size() >= 5 ? a.substr(a.size() - 5) : a;
        }

        // --- Status text buffer (returned as const char*) ---
        static char statusBuf[48];

        // ------------------------------------------------------------------
        // Scan callback — SCANNING state only
        // Collects connectable Apple devices into candidates[].
        // Transitions to CONNECTING automatically when the preferred address
        // is rediscovered.
        // ------------------------------------------------------------------
        class AmsScanCallbacks : public NimBLEAdvertisedDeviceCallbacks
        {
        public:
            void onResult(NimBLEAdvertisedDevice *dev) override
            {
                // Stop collecting once we have a target
                if (state != BtState::SCANNING) return;
                if (!dev->isConnectable()) return;

                const std::string &mfg = dev->getManufacturerData();
                if (mfg.size() < 2) return;
                uint16_t company = (uint8_t)mfg[0] | ((uint8_t)mfg[1] << 8);
                if (company != 0x004C) return; // Apple only

                // Skip known non-phone Apple devices (AirPods, AppleTV, Apple Watch)
                if (mfg.size() >= 3)
                {
                    uint8_t appleType = (uint8_t)mfg[2];
                    if (appleType == 0x07 || appleType == 0x09 ||
                        appleType == 0x0B || appleType == 0x0C || appleType == 0x0D)
                    {
                        // Only log once per address to avoid flood on repeated advertisements
                        std::string addrKey = dev->getAddress().toString();
                        for (char &c : addrKey) c = tolower((unsigned char)c);
                        if (skippedDevices.find(addrKey) == skippedDevices.end())
                        {
                            skippedDevices.insert(addrKey);
                            Serial.printf("[BT] Skipping non-phone Apple device (type 0x%02X) addr=%s\n",
                                          appleType, addrKey.c_str());
                        }
                        return;
                    }
                }

                NimBLEAddress addr    = dev->getAddress();
                std::string   name    = dev->getName();
                std::string   addrStr = addr.toString();
                int8_t        rssi    = dev->getRSSI();
                uint8_t       type    = (uint8_t)mfg[2];

                // Deduplicate — update RSSI on re-advertisement
                for (auto &c : candidates)
                {
                    if (c.addr == addr)
                    {
                        c.rssi = rssi;
                        return;
                    }
                }
                if (candidates.size() >= MAX_CANDIDATES) return;

                candidates.push_back(BtCandidate(addr, name, rssi, type));
                Serial.printf("[BT] iPhone/iPad candidate %zu: '%s' %s rssi=%d type=0x%02X\n",
                              candidates.size(),
                              name.empty() ? "(no name)" : name.c_str(),
                              addrStr.c_str(), rssi, type);

                // Auto-select preferred (saved) address → go straight to CONNECTING
                if (!preferredAddr.empty() && addrStr == preferredAddr)
                {
                    selectedIdx = (int)candidates.size() - 1;
                    TargetAddr  = addr;
                    state       = BtState::CONNECTING;
                    NimBLEDevice::getScan()->stop();
                    Serial.println("[BT] Preferred device found, auto-connecting");
                }
            }
        };

        // ------------------------------------------------------------------
        // ConnectToAms — called only when state == CONNECTING
        // On success  → state = CONNECTED
        // On failure  → state = SCANNING (advances selectedIdx, restarts scan)
        // ------------------------------------------------------------------
        bool ConnectToAms()
        {
            // scan->stop() is asynchronous; wait up to 300 ms for it to settle
            {
                NimBLEScan *s = NimBLEDevice::getScan();
                if (s && s->isScanning())
                {
                    Serial.println("[BT] Scan still running, stopping and waiting...");
                    s->stop();
                }
                uint32_t deadline = millis() + 300;
                while (NimBLEDevice::getScan()->isScanning() && millis() < deadline)
                    delay(10);
            }

            if (Client)
            {
                NimBLEDevice::deleteClient(Client);
                Client = nullptr;
            }

            Serial.printf("[BT] Connecting to: %s\n", TargetAddr.toString().c_str());

            Client = NimBLEDevice::createClient();
            if (!Client)
            {
                Serial.println("[BT] Failed to create client");
                state = BtState::SCANNING;
                NimBLEDevice::getScan()->start(0, false);
                return false;
            }
            Client->setConnectTimeout(5);

            // On any failure: advance to next candidate and restart scan
            auto advanceAndRescan = [&](const char *reason)
            {
                Serial.printf("[BT] Candidate %d/%zu failed: %s — trying next\n",
                              selectedIdx + 1, candidates.size(), reason);
                if (!candidates.empty())
                    selectedIdx = (selectedIdx + 1) % (int)candidates.size();
                state = BtState::SCANNING;
                NimBLEScan *s = NimBLEDevice::getScan();
                if (s) s->start(0, false);
            };

            if (!Client->connect(TargetAddr))
            {
                NimBLEDevice::deleteClient(Client);
                Client = nullptr;
                advanceAndRescan("connect() failed");
                return false;
            }

            Serial.printf("[BT] Connected to %s, securing...\n",
                          TargetAddr.toString().c_str());

            if (!Client->secureConnection())
            {
                Client->disconnect();
                NimBLEDevice::deleteClient(Client);
                Client = nullptr;
                advanceAndRescan("secureConnection() failed (wrong device or rejected)");
                return false;
            }

            Serial.println("[BT] Secure connection OK, starting AMS...");

            if (!AppleMediaService::StartMediaService(Client))
            {
                Client->disconnect();
                NimBLEDevice::deleteClient(Client);
                Client = nullptr;
                advanceAndRescan("AMS service not found (not an iPhone?)");
                return false;
            }

            Serial.println("[BT] AMS started");

            // Save preferred address on first successful connection
            {
                std::string addrStr = TargetAddr.toString();
                if (addrStr != preferredAddr)
                {
                    preferredAddr = addrStr;
                    SavePreferredAddr(preferredAddr);
                }
            }

            // Current Time Service
            CurrentTimeService::CurrentTime time;
            if (!CurrentTimeService::StartTimeService(Client, &time))
            {
                Serial.println("[BT] StartTimeService failed");
            }
            else
            {
                timeval new_time;
                new_time.tv_sec  = time.ToTimeT();
                new_time.tv_usec = (long)(time.mSecondsFraction * 1000000.0f);
                Serial.printf("[BT] TIME: unix=%ld usec=%ld\n",
                              (long)new_time.tv_sec, (long)new_time.tv_usec);
                if (settimeofday(&new_time, nullptr) == 0)
                    TimeSet = true;
                else
                    Serial.println("[BT] Error setting time of day");
                time.Dump();
            }

            state = BtState::CONNECTED;
            Serial.println("[BT] ConnectToAms: success");
            return true;
        }

    } // anonymous namespace

    // ------------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------------

    void ClearBonds()
    {
        Serial.println("[BLE] Clearing all BLE bonds...");
        NimBLEDevice::deleteAllBonds();

        if (Client)
        {
            if (Client->isConnected())
                Client->disconnect();
            NimBLEDevice::deleteClient(Client);
            Client = nullptr;
        }

        TimeSet       = false;
        lastAutoTryMs = millis(); // give 4 s for scan to collect candidates before auto-try
        state         = BtState::SCANNING;

        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan)
        {
            Serial.println("[BLE] Restarting scan after clearbonds...");
            scan->start(0, false);
        }

        Serial.println("[BLE] Bonds cleared. On iPhone: Settings → Bluetooth → Forget this device, then re-pair.");
    }

    void Begin(const std::string &device_name)
    {
        Serial.println("[BT] Begin() AMS central-only");
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        state     = BtState::IDLE;
        Ended     = false;
        TimeSet   = false;
        candidates.clear();
        skippedDevices.clear();
        selectedIdx   = 0;
        lastAutoTryMs = 0;

        preferredAddr = LoadPreferredAddr();
        if (!preferredAddr.empty())
            Serial.printf("[BT] Will auto-connect to saved device: %s\n", preferredAddr.c_str());

        Serial.printf("[BT] t=%lums NimBLEDevice::init...\n", millis());
        NimBLEDevice::init(device_name);
        Serial.printf("[BT] t=%lums init done\n", millis());

        // bonding=false → iOS pairs with "Just Works" encryption, no persistent bond
        NimBLEDevice::setSecurityAuth(/*bonding=*/false, /*mitm=*/false, /*sc=*/true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

        NimBLEScan *scan = NimBLEDevice::getScan();
        static AmsScanCallbacks cb;
        scan->setAdvertisedDeviceCallbacks(&cb);
        scan->setActiveScan(true);
        scan->setInterval(45);
        scan->setWindow(30);
        scan->setDuplicateFilter(false);
        scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);

        state = BtState::SCANNING; // set BEFORE scan->start so Service() runs immediately
        Serial.printf("[BT] t=%lums scan->start...\n", millis());
        scan->start(0, false); // 0 = forever; blocks until scan->stop() is called
        Serial.printf("[BT] t=%lums scan->start returned (scan stopped)\n", millis());
        Serial.println("[BT] Begin finished");
    }

    void End()
    {
        Serial.println("[BT] End()");
        Ended = true;
        state = BtState::IDLE;

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

    // ------------------------------------------------------------------
    // Service() — call from main loop() every iteration
    //
    //  IDLE       → noop (Begin() not done yet)
    //  SCANNING   → wait for candidates; auto-cycle every 4 s
    //  CONNECTING → attempt connection (blocks briefly for scan-stop)
    //  CONNECTED  → watch for drop; clean up and rescan if dropped
    // ------------------------------------------------------------------
    void Service()
    {
        if (Ended || state == BtState::IDLE) return;

        switch (state)
        {
        case BtState::SCANNING:
            if (!candidates.empty())
            {
                uint32_t now = millis();
                if (now - lastAutoTryMs > 4000)
                {
                    lastAutoTryMs = now;
                    TargetAddr    = candidates[selectedIdx].addr;
                    state         = BtState::CONNECTING;
                    NimBLEScan *s = NimBLEDevice::getScan();
                    if (s && s->isScanning()) s->stop();
                    Serial.printf("[BT] Auto-trying candidate %d/%zu: %s '%s'\n",
                                  selectedIdx + 1, candidates.size(),
                                  candidates[selectedIdx].addr.toString().c_str(),
                                  candidates[selectedIdx].name.empty()
                                      ? "(no name)" : candidates[selectedIdx].name.c_str());
                }
            }
            break;

        case BtState::CONNECTING:
            ConnectToAms();
            break;

        case BtState::CONNECTED:
            if (!Client || !Client->isConnected())
            {
                Serial.println("[BT] Connection dropped, restarting scan");
                if (Client)
                {
                    NimBLEDevice::deleteClient(Client);
                    Client = nullptr;
                }
                TimeSet = false;
                candidates.clear();
                skippedDevices.clear();
                selectedIdx   = 0;
                lastAutoTryMs = 0;
                state         = BtState::SCANNING;
                NimBLEScan *scan = NimBLEDevice::getScan();
                if (scan && !scan->isScanning())
                    scan->start(0, false);
            }
            break;

        default:
            break;
        }
    }

    bool IsConnected()
    {
        return state == BtState::CONNECTED && Client && Client->isConnected();
    }

    bool IsTimeSet()
    {
        return TimeSet;
    }

    const char *GetStatusText()
    {
        switch (state)
        {
        case BtState::CONNECTED:
            return "Connected";

        case BtState::CONNECTING:
            if (!candidates.empty() && selectedIdx < (int)candidates.size())
            {
                snprintf(statusBuf, sizeof(statusBuf), "Connecting %d/%zu: %s",
                         selectedIdx + 1, candidates.size(),
                         CandidateLabel(candidates[selectedIdx]).c_str());
            }
            else
                snprintf(statusBuf, sizeof(statusBuf), "Connecting...");
            return statusBuf;

        case BtState::SCANNING:
            if (!candidates.empty())
            {
                snprintf(statusBuf, sizeof(statusBuf), "%d/%zu: %s",
                         selectedIdx + 1, candidates.size(),
                         CandidateLabel(candidates[selectedIdx]).c_str());
                return statusBuf;
            }
            return "Scanning...";

        default:
            return "Initialising...";
        }
    }

    void SelectNext()
    {
        if (candidates.empty()) return;
        selectedIdx = (selectedIdx + 1) % (int)candidates.size();
        Serial.printf("[BT] SelectNext -> %d/%zu (%s)\n",
                      selectedIdx + 1, candidates.size(),
                      CandidateLabel(candidates[selectedIdx]).c_str());
    }

    void SelectPrev()
    {
        if (candidates.empty()) return;
        selectedIdx = (selectedIdx + (int)candidates.size() - 1) % (int)candidates.size();
        Serial.printf("[BT] SelectPrev -> %d/%zu (%s)\n",
                      selectedIdx + 1, candidates.size(),
                      CandidateLabel(candidates[selectedIdx]).c_str());
    }

    void SelectByIndex(int idx)
    {
        if (candidates.empty() || idx < 0 || idx >= (int)candidates.size()) return;
        selectedIdx = idx;
        TargetAddr  = candidates[selectedIdx].addr;
        state       = BtState::CONNECTING;
        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan && scan->isScanning()) scan->stop();
        Serial.printf("[BT] SelectByIndex %d/%zu: %s\n",
                      selectedIdx + 1, candidates.size(),
                      candidates[selectedIdx].addr.toString().c_str());
    }

    void ConnectSelected()
    {
        if (candidates.empty()) return;
        TargetAddr  = candidates[selectedIdx].addr;
        state       = BtState::CONNECTING;
        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan && scan->isScanning()) scan->stop();
        Serial.printf("[BT] ConnectSelected: %s\n", TargetAddr.toString().c_str());
    }

    void ForgetDevice()
    {
        Serial.println("[BT] ForgetDevice: clearing preferred addr and bonds");
        ClearPreferredAddr();
        NimBLEDevice::deleteAllBonds();

        if (Client)
        {
            if (Client->isConnected()) Client->disconnect();
            NimBLEDevice::deleteClient(Client);
            Client = nullptr;
        }

        TimeSet       = false;
        candidates.clear();
        selectedIdx   = 0;
        lastAutoTryMs = millis(); // give 4 s for scan to collect candidates before auto-try
        state         = BtState::SCANNING;

        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan && !scan->isScanning())
            scan->start(0, false);
    }

    String GetStatusJson()
    {
        String json = "{";
        bool conn = state == BtState::CONNECTED && Client && Client->isConnected();
        json += "\"connected\":" + String(conn ? "true" : "false") + ",";
        json += "\"status\":\"" + String(GetStatusText()) + "\",";
        json += "\"candidates\":[";
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (i > 0) json += ",";
            std::string label = CandidateLabel(candidates[i]);
            std::string addr  = candidates[i].addr.toString();
            char typeBuf[5];
            snprintf(typeBuf, sizeof(typeBuf), "0x%02X", candidates[i].mfgType);
            json += "{\"name\":\"" + String(label.c_str()) + "\",";
            json += "\"addr\":\"" + String(addr.c_str()) + "\",";
            json += "\"rssi\":" + String(candidates[i].rssi) + ",";
            json += "\"type\":\"" + String(typeBuf) + "\",";
            json += "\"selected\":" + String((int)i == selectedIdx ? "true" : "false") + "}";
        }
        json += "]}";
        return json;
    }

} // namespace Bluetooth
