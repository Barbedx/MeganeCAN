
#include <Arduino.h>
#include "bluetooth.h"
#include <NimBLEDevice.h>
#include <Preferences.h>

#include <time.h>
#include <sys/time.h>

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
        NimBLEAddress        TargetAddr; // set before each connect attempt

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
        static constexpr size_t         MAX_CANDIDATES = 20;

        // Auto-try timer (lives here so it can be reset on reconnect)
        static uint32_t lastAutoTryMs = 0;

        // Settle timer: small extra buffer (200 ms) started in onDisconnect after the
        // server-side connection fully closes.  ConnectToAms() must not run until
        // _serverDisconnectComplete is true AND this timer has elapsed.
        static uint32_t _connectSettleUntilMs = 0;

        // Set by onDisconnect (when state==CONNECTING) to signal that NimBLE has
        // fully processed the server disconnect HCI event — safe to open a client
        // connection now.  This is the authoritative gate; a flat settle timer is
        // insufficient because ble_gap_is_preempted() stays set until the host
        // processes the event asynchronously.
        // Starts true so non-server paths (scan auto-connect, SelectByIndex,
        // ConnectSelected) go straight through without waiting.
        static bool _serverDisconnectComplete = true;

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

        // --- BLE server (server+client pattern for AMS) ---
        // iOS connects to our server when it sees the solicited AMS UUID in our
        // advertisement. This gives us iOS's current (non-rotated) address so we
        // can open a fresh client connection and find AMS reliably.
        //
        // IMPORTANT (from ReflectionsOS NimBLE guidance):
        //  • Never initiate a client connection from inside a NimBLE callback.
        //    Set a flag; let Service() handle the actual connect on the next loop.
        //  • Stop advertising before connecting as a client — they compete for radio.
        //  • Disconnect the server connection from Service(), not from onConnect.
        static NimBLEServer  *_server             = nullptr;
        static bool           _pendingIosConnect   = false;
        static NimBLEAddress  _pendingIosAddr;
        static uint16_t       _pendingConnHandle   = 0;

        class AmsServerCallbacks : public NimBLEServerCallbacks
        {
        public:
            void onConnect(NimBLEServer *, NimBLEConnInfo &connInfo) override
            {
                // Capture iOS address immediately. Service() SCANNING will
                // stop advertising/scan, disconnect the server connection,
                // then ConnectToAms() once onDisconnect confirms it's done.
                _pendingIosAddr    = connInfo.getAddress();
                _pendingConnHandle = connInfo.getConnHandle();
                _pendingIosConnect = true;
                _serverDisconnectComplete = false;
                Serial.printf("[BT] Server: iOS connected from %s\n",
                              _pendingIosAddr.toString().c_str());
            }
            void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override
            {
                _pendingIosConnect = false;
                Serial.printf("[BT] Server: iOS disconnected (reason=%d), state=%d\n",
                              reason, (int)state);
                if (state == BtState::CONNECTING) {
                    // We intentionally disconnected the server to open client connection.
                    // NimBLE has now fully processed the HCI disconnect event — safe
                    // to call Client->connect(). Add a small buffer for the stack to
                    // finish any internal cleanup before we open the client connection.
                    _serverDisconnectComplete = true;
                    _connectSettleUntilMs = millis() + 200;
                    Serial.println("[BT] Server disconnect done — will connect as client in 200ms");
                } else {
                    // Unexpected disconnect while still scanning — restart advertising.
                    NimBLEDevice::getAdvertising()->start();
                    Serial.println("[BT] Advertising restarted after server disconnect");
                }
            }
        };
        static AmsServerCallbacks _serverCbs;

        // ------------------------------------------------------------------
        // Scan callback — SCANNING state only
        // Collects connectable Apple devices into candidates[].
        // Transitions to CONNECTING automatically when the preferred address
        // is rediscovered.
        // ------------------------------------------------------------------
        class AmsScanCallbacks : public NimBLEScanCallbacks
        {
        public:
            void onResult(const NimBLEAdvertisedDevice *dev) override
            {
                // Stop collecting once we have a target
                if (state != BtState::SCANNING) return;
                if (!dev->isConnectable()) return;

                const std::string &mfg = dev->getManufacturerData();
                if (mfg.size() < 2) return;
                uint16_t company = (uint8_t)mfg[0] | ((uint8_t)mfg[1] << 8);
                if (company != 0x004C) return; // Apple only

                // No type filtering — Apple's type byte mapping is undocumented and unreliable.
                // 0x10 = Nearby Info (iPhone), 0x0C = Handoff (iPhone/Mac), 0x0D = Nearby Action
                // (iPhone), 0x07 = AirPods proximity pairing, etc. — we can't reliably tell
                // which is the user's phone from type alone. Collect all; let the user pick.

                NimBLEAddress addr    = dev->getAddress();
                std::string   name    = dev->getName();
                std::string   addrStr = addr.toString();
                int8_t        rssi    = dev->getRSSI();
                uint8_t       type    = (mfg.size() >= 3) ? (uint8_t)mfg[2] : 0;

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
            // Stop advertising — advertising and active client connections fight for radio time
            NimBLEDevice::getAdvertising()->stop();

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

            Client = NimBLEDevice::createClient();
            if (!Client)
            {
                Serial.println("[BT] Failed to create client");
                state = BtState::SCANNING;
                NimBLEDevice::getScan()->start(0, false, false);
                return false;
            }
            // Connection params from article: 32*0.625ms=20ms min, 160*0.625ms=100ms max,
            // 0 latency, 500*10ms=5s supervision timeout
            Client->setConnectionParams(32, 160, 0, 500);
            Client->setConnectTimeout(15); // 15s timeout

            // On any failure: advance to next candidate, restart scan + advertising
            auto advanceAndRescan = [&](const char *reason)
            {
                Serial.printf("[BT] Candidate %d/%zu failed: %s — trying next\n",
                              selectedIdx + 1, candidates.size(), reason);
                if (!candidates.empty())
                    selectedIdx = (selectedIdx + 1) % (int)candidates.size();
                lastAutoTryMs = millis(); // reset timer so we wait 4 s before next attempt
                state = BtState::SCANNING;
                NimBLEScan *s = NimBLEDevice::getScan();
                if (s) s->start(0, false, false);
                NimBLEDevice::getAdvertising()->start(); // re-attract iOS via server path
            };

            // Note: GAP preemption after server disconnect is handled upstream —
            // ConnectToAms() is only called once _serverDisconnectComplete is true
            // (set in onDisconnect) AND the 200ms settle timer has elapsed. By that
            // point NimBLE has processed the HCI disconnect event and rebuilt the
            // resolving list, so connect() should succeed immediately.

            Serial.printf("[BT] Connecting to: %s\n", TargetAddr.toString().c_str());
            bool connected = Client->connect(TargetAddr);
            int  lastErr   = connected ? 0 : Client->getLastError();

            if (!connected)
            {
                NimBLEDevice::deleteClient(Client);
                Client = nullptr;
                char reason[32];
                snprintf(reason, sizeof(reason), "connect() failed (err=%d)", lastErr);
                advanceAndRescan(reason);
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
                time_t unix = time.ToTimeT();
                // Sanity check: reject garbage timestamps (year < 2020 or > 2100)
                if (time.mYear < 2020 || time.mYear > 2100)
                {
                    Serial.printf("[BT] CTS returned invalid year=%u — ignoring\n", time.mYear);
                }
                else
                {
                    timeval new_time;
                    new_time.tv_sec  = unix;
                    new_time.tv_usec = (long)(time.mSecondsFraction * 1000000.0f);
                    Serial.printf("[BT] TIME: unix=%ld usec=%ld\n",
                                  (long)new_time.tv_sec, (long)new_time.tv_usec);
                    time.Dump();
                    if (settimeofday(&new_time, nullptr) == 0)
                        TimeSet = true;
                    else
                        Serial.println("[BT] Error setting time of day");
                }
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
            scan->start(0, false, false);
        }
        NimBLEDevice::getAdvertising()->start();

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
        selectedIdx               = 0;
        lastAutoTryMs             = 0;
        _serverDisconnectComplete = true;  // no pending server disconnect at boot
        _connectSettleUntilMs     = 0;

        preferredAddr = LoadPreferredAddr();
        if (!preferredAddr.empty())
            Serial.printf("[BT] Will auto-connect to saved device: %s\n", preferredAddr.c_str());

        Serial.printf("[BT] t=%lums NimBLEDevice::init...\n", millis());
        NimBLEDevice::init(device_name);
        Serial.printf("[BT] t=%lums init done\n", millis());

        // bonding=true → iOS stores our device; required for AMS to be exposed
        NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

        // ---- BLE server: iOS connects to us when it sees our solicited AMS UUID ----
        // We only need onConnect to fire so we can capture iOS's current address.
        // No services are needed — the server is a "doorbell" only.
        _server = NimBLEDevice::createServer();
        _server->setCallbacks(&_serverCbs);
        _server->start();

        // Advertise with solicited AMS UUID (GAP type 0x15 — 128-bit solicited services).
        // iOS sees this and:
        //   a) connects to our server so we get its current non-rotated address, AND
        //   b) knows to expose AMS when we connect to it as a client.
        {
            // AMS UUID 89D3502B-0F36-433A-8EF4-C502AD55F8DC in little-endian BLE order
            static const uint8_t solicit[] = {
                17, 0x15,                           // length=17, GAP type=0x15
                0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5,
                0xF4, 0x8E, 0x3A, 0x43, 0x36, 0x0F,
                0x2B, 0x50, 0xD3, 0x89
            };
            NimBLEAdvertising    *adv = NimBLEDevice::getAdvertising();
            NimBLEAdvertisementData advData;
            advData.setFlags(0x06); // LE General Discoverable, BR/EDR Not Supported
            advData.addData(solicit, sizeof(solicit));
            adv->setAdvertisementData(advData);

            // Scan response: name + manufacturer data for easy filtering in BLE scanner apps
            // (LightBlue, nRF Connect — filter by "MCan" or company "Espressif Inc.")
            NimBLEAdvertisementData scanRsp;
            scanRsp.setName(device_name.c_str());
            // Manufacturer specific data: Espressif company ID (0x02E5) + "MCan"
            static const uint8_t mfgData[] = {
                7, 0xFF,               // length=7, GAP type=0xFF (manufacturer specific)
                0xE5, 0x02,            // company ID 0x02E5 (Espressif Inc.) little-endian
                'M', 'C', 'a', 'n'    // 4-byte identifier
            };
            scanRsp.addData(mfgData, sizeof(mfgData));
            adv->setScanResponseData(scanRsp);
            bool advOk = adv->start();
            Serial.printf("[BT] Advertising with solicited AMS UUID — start()=%s  name=\"%s\"\n",
                          advOk ? "OK" : "FAIL", device_name.c_str());
        }

        NimBLEScan *scan = NimBLEDevice::getScan();
        static AmsScanCallbacks cb;
        scan->setScanCallbacks(&cb);
        scan->setActiveScan(false); // passive scan — we only need addr+mfg data, not name
        // Low-duty-cycle scan: 18.75ms window every 500ms = ~4% scan time.
        // Leaves the radio free to advertise (so iPhone can see "MeganeCAN" in BT Settings).
        scan->setInterval(800); // 500ms interval
        scan->setWindow(30);    // 18.75ms window
        scan->setDuplicateFilter(false);
        scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);

        state = BtState::SCANNING; // set BEFORE scan->start so Service() runs immediately
        Serial.printf("[BT] t=%lums scan->start...\n", millis());
        scan->start(0, false, false); // non-blocking in NimBLE 2.x — returns immediately
        Serial.printf("[BT] t=%lums scan started\n", millis());
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

        // Periodic heartbeat — printed every 5 s so we can confirm the loop is running
        // and advertising is active.
        {
            static uint32_t lastHb = 0;
            uint32_t now = millis();
            if (now - lastHb > 5000) {
                lastHb = now;
                const char *stateStr =
                    state == BtState::SCANNING   ? "SCANNING"   :
                    state == BtState::CONNECTING ? "CONNECTING" :
                    state == BtState::CONNECTED  ? "CONNECTED"  : "IDLE";
                NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
                Serial.printf("[BT] heartbeat state=%s candidates=%zu adv=%s server=%s\n",
                              stateStr, candidates.size(),
                              adv->isAdvertising() ? "ON" : "OFF",
                              _server ? "UP" : "NULL");
            }
        }

        switch (state)
        {
        case BtState::SCANNING:
        {
            // Priority 1: iOS connected to our server → we have its current address.
            // Immediately stop advertising + scan, disconnect server, then wait
            // for onDisconnect to confirm NimBLE finished the HCI event before
            // opening the client connection. No bonding wait — connecting quickly
            // uses the fresh address before iOS has any reason to rotate it.
            if (_pendingIosConnect)
            {
                _pendingIosConnect        = false;
                _serverDisconnectComplete = false;
                TargetAddr = _pendingIosAddr;
                Serial.printf("[BT] iOS connected to server — stopping adv+scan, disconnecting server, will connect to %s\n",
                              TargetAddr.toString().c_str());
                NimBLEDevice::getAdvertising()->stop();
                NimBLEScan *sc = NimBLEDevice::getScan();
                if (sc && sc->isScanning()) sc->stop();
                // Transition first so onDisconnect (state==CONNECTING) sets the flag
                // and doesn't restart advertising.
                state = BtState::CONNECTING;
                if (_server) _server->disconnect(_pendingConnHandle);
                break;
            }

            // Priority 2: scan found our previously-saved device (handled in scan callback
            // which sets state=CONNECTING and TargetAddr directly — no code needed here).

            // NO blind auto-cycling through unknown Apple devices.
            // Connecting to random neighbours' iPhones/MacBooks wastes time and stops
            // advertising, preventing the user's iPhone from seeing "MeganeCAN" to pair.
            // Manual selection is available via steering wheel (SelectNext/ConnectSelected)
            // or the web UI.

            // Keep advertising visible while idle in SCANNING state.
            // Call start() periodically — NimBLE 2.x ignores it if already advertising.
            {
                static uint32_t lastAdvEnsure = 0;
                uint32_t now = millis();
                if (now - lastAdvEnsure > 2000) {
                    lastAdvEnsure = now;
                    NimBLEDevice::getAdvertising()->start();
                }
            }
            break;
        }

        case BtState::CONNECTING:
            // If we got here via the server path: wait for onDisconnect to confirm
            // NimBLE has processed the HCI disconnect event before connecting as client.
            // (_serverDisconnectComplete starts false, set to true in onDisconnect)
            // If we got here via scan/manual path: _serverDisconnectComplete is already
            // true (no server to disconnect), so this check passes immediately.
            if (!_serverDisconnectComplete) break;
            if (_connectSettleUntilMs && millis() < _connectSettleUntilMs) break;
            _connectSettleUntilMs = 0;
            ConnectToAms();
            break;

        case BtState::CONNECTED:
            if (!Client || !Client->isConnected())
            {
                Serial.println("[BT] Connection dropped, restarting scan + advertising");
                if (Client)
                {
                    NimBLEDevice::deleteClient(Client);
                    Client = nullptr;
                }
                TimeSet = false;
                candidates.clear();
                        selectedIdx   = 0;
                lastAutoTryMs = 0;
                state         = BtState::SCANNING;
                NimBLEScan *scan = NimBLEDevice::getScan();
                if (scan && !scan->isScanning())
                    scan->start(0, false, false);
                // Resume advertising so iOS reconnects via server+client path
                NimBLEDevice::getAdvertising()->start();
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
        NimBLEDevice::getAdvertising()->stop();
        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan && scan->isScanning()) scan->stop();
        state = BtState::CONNECTING;
        Serial.printf("[BT] SelectByIndex %d/%zu: %s\n",
                      selectedIdx + 1, candidates.size(),
                      candidates[selectedIdx].addr.toString().c_str());
    }

    void ConnectSelected()
    {
        if (candidates.empty()) return;
        TargetAddr  = candidates[selectedIdx].addr;
        NimBLEDevice::getAdvertising()->stop();
        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan && scan->isScanning()) scan->stop();
        state = BtState::CONNECTING;
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
            scan->start(0, false, false);
        NimBLEDevice::getAdvertising()->start();
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
