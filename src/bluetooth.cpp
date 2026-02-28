
#include <Arduino.h>
#include "bluetooth.h"
#include <NimBLEDevice.h>
#include <Preferences.h>

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
    namespace
    {
        // --- State ---
        bool Ended = false;
        bool Connected = false;

        BLEClient *Client = nullptr;
        BLEAddress TargetAddr{"00:00:00:00:00:00"};
        bool TargetFound = false;

        RTC_DATA_ATTR bool TimeSet = false;

        // --- Candidate list ---
        struct BtCandidate { NimBLEAddress addr; std::string name; };
        static std::vector<BtCandidate> candidates;
        static int selectedIdx = 0;
        static std::string preferredAddr; // saved address from NVS

        static constexpr size_t MAX_CANDIDATES = 8;

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
        // Apple doesn't always broadcast device name, so fall back to last 5 chars of address
        static std::string CandidateLabel(const BtCandidate &c)
        {
            if (!c.name.empty())
                return c.name;
            // use last 5 chars of address (e.g. "a1:b2")
            std::string a = c.addr.toString();
            return a.size() >= 5 ? a.substr(a.size() - 5) : a;
        }

        // --- Status text buffer (returned as const char*) ---
        static char statusBuf[48];

        // --- Scan callback: collect all Apple (0x004C) connectable devices ---
        class AmsScanCallbacks : public NimBLEAdvertisedDeviceCallbacks
        {
        public:
            void onResult(NimBLEAdvertisedDevice *dev) override
            {
                if (TargetFound) return;
                if (!dev->isConnectable()) return;

                const std::string& mfg = dev->getManufacturerData();
                if (mfg.size() < 2) return;
                uint16_t company = (uint8_t)mfg[0] | ((uint8_t)mfg[1] << 8);
                if (company != 0x004C) return; // Apple only

                NimBLEAddress addr = dev->getAddress();
                std::string   name = dev->getName();
                std::string   addrStr = addr.toString();

                // Deduplicate by address
                for (auto& c : candidates)
                    if (c.addr == addr) return;
                if (candidates.size() >= MAX_CANDIDATES) return;

                candidates.push_back({addr, name});
                Serial.printf("[BT] Apple device %zu: '%s' %s\n",
                              candidates.size(),
                              name.empty() ? "(no name)" : name.c_str(),
                              addrStr.c_str());

                // Auto-select preferred (saved) address
                if (!preferredAddr.empty() && addrStr == preferredAddr)
                {
                    selectedIdx = (int)candidates.size() - 1;
                    TargetAddr  = addr;
                    TargetFound = true;
                    NimBLEDevice::getScan()->stop();
                    Serial.println("[BT] Preferred device found, auto-connecting");
                }
            }
        };

        bool ConnectToAms()
        {
            if (!TargetFound)
            {
                Serial.println("ConnectToAms: no target found yet");
                return false;
            }

            if (Client)
            {
                Serial.println("ConnectToAms: deleting existing client");
                NimBLEDevice::deleteClient(Client);
                Client = nullptr;
            }

            Serial.print("Connecting to AMS device: ");
            Serial.println(TargetAddr.toString().c_str());

            Client = NimBLEDevice::createClient();
            if (!Client)
            {
                Serial.println("Failed to create client");
                return false;
            }

            if (!Client->connect(TargetAddr))
            {
                Serial.println("Client connect failed");
                NimBLEDevice::deleteClient(Client);
                Client = nullptr;

                Connected = false;
                TargetFound = false;

                NimBLEScan *s = NimBLEDevice::getScan();
                if (s)
                {
                    Serial.println("Restarting scan after failed connect");
                    s->start(0, false);
                }
                return false;
            }

            Serial.println("Client connected to AMS device");

            // Synchronous security: fail fast if pairing/encryption fails
            if (!Client->secureConnection()) {
                Serial.println("Failed to secure connection (pairing/encryption)");
                Client->disconnect();
                NimBLEDevice::deleteClient(Client);
                Client = nullptr;

                Connected   = false;
                TargetFound = false;

                NimBLEScan *s = NimBLEDevice::getScan();
                if (s) {
                    Serial.println("Restarting scan after failed secureConnection");
                    s->start(0, false);
                }
                return false;
            }

            // --- Apple Media Service ---
            if (!AppleMediaService::StartMediaService(Client))
            {
                Serial.println("StartMediaService failed (AMS not available?)");

                Client->disconnect();
                NimBLEDevice::deleteClient(Client);
                Client = nullptr;

                Connected = false;
                TargetFound = false;

                NimBLEScan *s = NimBLEDevice::getScan();
                if (s)
                {
                    Serial.println("Restarting scan after bad AMS device");
                    s->start(0, false);
                }
                return false;
            }
            else
            {
                Serial.println("AMS started");
            }

            // --- Save preferred address on successful AMS connection ---
            {
                std::string addrStr = TargetAddr.toString();
                if (addrStr != preferredAddr)
                {
                    preferredAddr = addrStr;
                    SavePreferredAddr(preferredAddr);
                }
            }

            // --- Current Time Service ---
            CurrentTimeService::CurrentTime time;
            if (!CurrentTimeService::StartTimeService(Client, &time))
            {
                Serial.println("StartTimeService failed");
            }
            else
            {
                timeval new_time;
                new_time.tv_sec = time.ToTimeT();
                new_time.tv_usec = static_cast<long>(time.mSecondsFraction * 1000000.0f);

                Serial.printf("TIME: unix=%ld, usec=%ld\n",
                              (long)new_time.tv_sec,
                              (long)new_time.tv_usec);

                if (settimeofday(&new_time, nullptr) != 0)
                {
                    Serial.println("Error setting time of day");
                }
                else
                {
                    TimeSet = true;
                }

                time.Dump();
            }

            Connected = true;
            Serial.println("ConnectToAms finished");
            return true;
        }

    } // anonymous namespace

    // ---------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------
    void ClearBonds()
    {
        Serial.println("[BLE] Clearing all BLE bonds...");

        NimBLEDevice::deleteAllBonds();

        if (Client) {
            if (Client->isConnected()) {
                Serial.println("[BLE] Disconnecting active client...");
                Client->disconnect();
            }
            NimBLEDevice::deleteClient(Client);
            Client = nullptr;
        }

        Connected   = false;
        TargetFound = false;
        TimeSet     = false;

        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan) {
            Serial.println("[BLE] Restarting scan after clearbonds...");
            scan->start(0, false);
        }

        Serial.println("[BLE] Bonds cleared. On iPhone: Settings → Bluetooth → Forget this device, then re-pair.");
    }

    void Begin(const std::string &device_name)
    {
        Serial.println("Bluetooth::Begin() AMS central-only");
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        Ended = false;
        Connected = false;
        TimeSet = false;
        TargetFound = false;
        candidates.clear();
        selectedIdx = 0;

        // Load saved preferred address for auto-reconnect
        preferredAddr = LoadPreferredAddr();
        if (!preferredAddr.empty())
            Serial.printf("[BT] Will auto-connect to saved device: %s\n", preferredAddr.c_str());

        NimBLEDevice::init(device_name);

        NimBLEDevice::setSecurityAuth(/*bonding=*/false, /*mitm=*/false, /*sc=*/true);

        // No input/output → iOS does "Just Works" pairing
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

        NimBLEScan *scan = NimBLEDevice::getScan();
        static AmsScanCallbacks cb;

        scan->setAdvertisedDeviceCallbacks(&cb);
        scan->setActiveScan(true);
        scan->setInterval(45);
        scan->setWindow(30);
        scan->setDuplicateFilter(false);
        scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
        scan->start(0, false); // 0 = forever

        Serial.println("Bluetooth::Begin finished (scanning for AMS)");
    }

    void End()
    {
        Serial.println("Bluetooth::End()");

        Ended = true;
        Connected = false;
        TargetFound = false;

        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan)
            scan->stop();

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
        if (Ended)
            return;

        // If we had a client and it dropped, clean up and rescan
        if (Client && !Client->isConnected())
        {
            Serial.println("Service: client disconnected, cleaning up");
            NimBLEDevice::deleteClient(Client);
            Client = nullptr;

            Connected = false;
            TimeSet = false;
            TargetFound = false;

            // Clear candidates so we rediscover fresh
            candidates.clear();
            selectedIdx = 0;

            NimBLEScan *scan = NimBLEDevice::getScan();
            if (scan && !scan->isScanning())
            {
                Serial.println("Service: restarting scan");
                scan->start(0, false);
            }
        }

        // If not connected but target is found → try to connect
        if ((!Client || !Client->isConnected()) && TargetFound)
        {
            Serial.println("Service: trying to connect to AMS target");
            ConnectToAms();
        }
    }

    bool IsConnected()
    {
        return Connected && Client && Client->isConnected();
    }

    bool IsTimeSet()
    {
        return TimeSet;
    }

    const char* GetStatusText()
    {
        if (Connected && Client && Client->isConnected())
            return "Connected";

        if (TargetFound)
        {
            if (!candidates.empty() && selectedIdx < (int)candidates.size())
            {
                std::string label = CandidateLabel(candidates[selectedIdx]);
                snprintf(statusBuf, sizeof(statusBuf), "Connecting %d/%zu: %s",
                         selectedIdx + 1, candidates.size(), label.c_str());
            }
            else
            {
                snprintf(statusBuf, sizeof(statusBuf), "Connecting...");
            }
            return statusBuf;
        }

        if (!candidates.empty())
        {
            // Show currently selected candidate and total count
            std::string label = CandidateLabel(candidates[selectedIdx]);
            snprintf(statusBuf, sizeof(statusBuf), "%d/%zu: %s",
                     selectedIdx + 1, candidates.size(), label.c_str());
            return statusBuf;
        }

        return "Scanning...";
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

    void ConnectSelected()
    {
        if (candidates.empty()) return;
        TargetAddr  = candidates[selectedIdx].addr;
        TargetFound = true;
        Serial.printf("[BT] ConnectSelected: %s\n", TargetAddr.toString().c_str());

        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan && scan->isScanning())
            scan->stop();
    }

    void ForgetDevice()
    {
        Serial.println("[BT] ForgetDevice: clearing preferred addr and bonds");
        ClearPreferredAddr();
        NimBLEDevice::deleteAllBonds();

        if (Client) {
            if (Client->isConnected()) Client->disconnect();
            NimBLEDevice::deleteClient(Client);
            Client = nullptr;
        }

        Connected   = false;
        TargetFound = false;
        TimeSet     = false;
        candidates.clear();
        selectedIdx = 0;

        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan && !scan->isScanning())
            scan->start(0, false);
    }

} // namespace Bluetooth
