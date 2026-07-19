#include "BleHub.h"
#include "HidRole.h"
#include "../bluetooth.h"
#include "../utils/Log.h"

#include <Preferences.h>
#include <string.h>

// Apple Media Service — solicited in the advertising packet so a fresh iPhone
// surfaces us in Settings > Bluetooth.
#define APPLE_MEDIA_SERVICE_UUID "89D3502B-0F36-433A-8EF4-C502AD55F8DC"

namespace BleHub
{
    namespace
    {
        Mode            gMode   = Mode::Ams;
        NimBLEServer   *gServer = nullptr;
        Ble::PeerTable  gPeers;
        char            gName[16] = {0};

        // Identity addresses learned across boots, so classification is instant on
        // reconnect instead of guessed. NVS namespace "ble".
        char gAmsAddr[18] = {0};
        char gHidAddr[18] = {0};

        void loadKnownAddrs()
        {
            Preferences p;
            if (!p.begin("ble", /*readOnly=*/true))
                return;
            p.getString("ams_addr", gAmsAddr, sizeof(gAmsAddr));
            p.getString("hid_addr", gHidAddr, sizeof(gHidAddr));
            p.end();
        }

        // NVS commits are flash erase+write — tens to hundreds of ms with the cache
        // disabled. Doing that on the BLE host task stalls it right in the middle of
        // the subscribe/encryption handshake and risks a supervision timeout. So the
        // callbacks only stage the address here; Service() (loop task) commits it.
        char gPendingAms[18] = {0};
        char gPendingHid[18] = {0};

        void stageKnownAddr(char *pending, const char *dst, const char *value)
        {
            if (!value || !value[0] || Ble::addrEq(dst, value))
                return;
            strncpy(pending, value, 17);
            pending[17] = 0;
        }

        void commitStagedAddrs()
        {
            const bool wantAms = gPendingAms[0] && !Ble::addrEq(gAmsAddr, gPendingAms);
            const bool wantHid = gPendingHid[0] && !Ble::addrEq(gHidAddr, gPendingHid);
            if (!wantAms && !wantHid)
            {
                gPendingAms[0] = gPendingHid[0] = 0;
                return;
            }
            Preferences p;
            if (!p.begin("ble", /*readOnly=*/false))
                return;
            if (wantAms)
            {
                strncpy(gAmsAddr, gPendingAms, 17);
                gAmsAddr[17] = 0;
                p.putString("ams_addr", gAmsAddr);
                LOGI("BLE", "remembered ams_addr = %s", gAmsAddr);
            }
            if (wantHid)
            {
                strncpy(gHidAddr, gPendingHid, 17);
                gHidAddr[17] = 0;
                p.putString("hid_addr", gHidAddr);
                LOGI("BLE", "remembered hid_addr = %s", gHidAddr);
            }
            p.end();
            gPendingAms[0] = gPendingHid[0] = 0;
        }

        // ---- advertising -------------------------------------------------------
        //
        // Budget is 31 bytes per packet. The primary carries flags(3) + name + the
        // 18-byte AMS solicitation = 27 for a 4-char name. The HID UUID and
        // appearance therefore CANNOT go in the primary in Both mode — they move to
        // the scan response, which Android reads happily. (A scan-response-only AMS
        // solicitation, by contrast, is NOT surfaced to a fresh iPhone — that was
        // established the hard way; do not "simplify" by swapping them.)
        // GAP appearance: Generic HID (0x03C0) | Keyboard subtype (0x01). Same value
        // as NimBLE's HID_KEYBOARD, spelled out so the hub does not have to pull in
        // NimBLEHIDDevice.h just for a constant.
        const uint16_t kAppearanceKeyboard = 0x03C1;

        enum class AdvProfile : uint8_t { AmsOnly, HidOnly, Dual };

        void applyAdvertising(AdvProfile profile)
        {
            NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
            if (!adv)
                return;
            adv->stop();

            NimBLEAdvertisementData advData;
            advData.setFlags(0x06); // LE General Discoverable, BR/EDR not supported
            advData.setName(gName);

            if (profile == AdvProfile::HidOnly)
            {
                advData.setAppearance(kAppearanceKeyboard);
                advData.addServiceUUID(NimBLEUUID((uint16_t)0x1812));
                adv->setAdvertisementData(advData);
                adv->enableScanResponse(false);
            }
            else
            {
                // AMS solicitation, hand-built: addData() takes a complete AD
                // structure with its own length byte -> [0x11][0x15][16-byte UUID].
                NimBLEUUID ams(APPLE_MEDIA_SERVICE_UUID);
                uint8_t sol[18];
                sol[0] = 0x11; // 1 (type) + 16 (uuid)
                sol[1] = 0x15; // 128-bit service solicitation
                memcpy(&sol[2], ams.getValue(), 16);
                advData.addData(sol, sizeof(sol));
                adv->setAdvertisementData(advData);

                if (profile == AdvProfile::Dual)
                {
                    NimBLEAdvertisementData sr;
                    sr.setAppearance(kAppearanceKeyboard);
                    sr.addServiceUUID(NimBLEUUID((uint16_t)0x1812));
                    adv->setScanResponseData(sr);
                    // m_scanResp defaults to false and is ONLY set here; without this
                    // the scan response is silently never transmitted.
                    adv->enableScanResponse(true);
                }
                else
                {
                    adv->enableScanResponse(false);
                }
            }

            adv->start();
            LOGI("BLE", "advertising '%s' profile=%s", gName,
                 profile == AdvProfile::Dual ? "dual" : (profile == AdvProfile::HidOnly ? "hid" : "ams"));
        }

        AdvProfile profileForMode()
        {
            switch (gMode)
            {
            case Mode::Keyboard: return AdvProfile::HidOnly;
            case Mode::Both:     return AdvProfile::Dual;
            default:             return AdvProfile::AmsOnly;
            }
        }

        // ---- callbacks ---------------------------------------------------------

        const char *reasonText(int reason)
        {
            switch (reason - 0x200) // strip BLE_HS_ERR_HCI_BASE
            {
            case 0x08: return "supervision timeout (out of range / RF / coexistence)";
            case 0x13: return "remote user terminated";
            case 0x16: return "terminated by local host (normal post-bond drop)";
            case 0x05: return "authentication failure (bond key mismatch)";
            case 0x06: return "PIN/key missing (one side lost the bond!)";
            case 0x3D: return "MIC failure (encryption key mismatch — stale bond!)";
            case 0x22: return "LMP/LL response timeout";
            case 0x3B: return "unacceptable connection parameters";
            default:   return "(see HCI error code)";
            }
        }

        class HubCallbacks : public NimBLEServerCallbacks
        {
            // Deliberately minimal: insert, relax, log. No getClient(), no service
            // discovery — those are GATT operations and belong on the loop task
            // (Service()), never in a host-task callback.
            void onConnect(NimBLEServer *s, NimBLEConnInfo &connInfo) override
            {
                const uint16_t h = connInfo.getConnHandle();
                // toString() returns a temporary std::string — copy out before it dies.
                char otaBuf[18] = {0};
                char idBuf[18]  = {0};
                strncpy(otaBuf, connInfo.getAddress().toString().c_str(), 17);
                strncpy(idBuf, connInfo.getIdAddress().toString().c_str(), 17);

                if (gPeers.count() >= maxPeers())
                {
                    LOGW("BLE", "mode allows %u peer(s), disconnecting h=%u",
                         (unsigned)maxPeers(), (unsigned)h);
                    s->disconnect(h);
                    return;
                }
                Ble::Peer *p = gPeers.add(h, otaBuf, idBuf);
                if (!p)
                {
                    LOGW("BLE", "peer table full, disconnecting h=%u", (unsigned)h);
                    s->disconnect(h);
                    return;
                }
                p->connectedMs = millis();
                p->encrypted   = connInfo.isEncrypted();

                // Classify immediately if we recognise the identity address.
                if (Ble::addrEq(p->idAddr, gHidAddr))
                    p->role = Ble::PeerRole::Hid;
                else if (Ble::addrEq(p->idAddr, gAmsAddr))
                    p->role = Ble::PeerRole::Ams;

                LOGI("BLE", "connected h=%u %s id=%s role=%s enc=%d bonds=%d",
                     (unsigned)h, p->addr, p->idAddr, Ble::roleName(p->role),
                     (int)p->encrypted, NimBLEDevice::getNumBonds());

                // WiFi and BLE share one radio. iOS defaults to a ~15-30ms interval
                // and starves the WiFi STA (dashboard becomes unreachable). Ask for
                // 30-50ms + slave latency 4 + 4s supervision — within Apple's rules
                // so iOS accepts it. Units: interval 1.25ms, timeout 10ms.
                s->updateConnParams(h, 24, 40, 4, 400);

                // Keep advertising while a slot is free so the second peer can find
                // us. advertiseOnDisconnect only covers the disconnect case.
                if (gPeers.count() < maxPeers())
                    applyAdvertising(profileForMode());
            }

            void onDisconnect(NimBLEServer *, NimBLEConnInfo &connInfo, int reason) override
            {
                const uint16_t h = connInfo.getConnHandle();
                Ble::Peer *p = gPeers.byHandle(h);
                const Ble::PeerRole role = p ? p->role : Ble::PeerRole::Unknown;
                const bool wasClientBound = p && p->clientBound;

                LOGI("BLE", "disconnected h=%u role=%s reason=%d (0x%X = %s) bonds=%d",
                     (unsigned)h, Ble::roleName(role), reason, reason, reasonText(reason),
                     NimBLEDevice::getNumBonds());

                gPeers.remove(h);

                // Only the AMS peer owns the cached remote characteristics.
                if (wasClientBound)
                    Bluetooth::OnAmsPeerLost();
            }

            void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
            {
                const uint16_t h = connInfo.getConnHandle();
                if (Ble::Peer *p = gPeers.byHandle(h))
                {
                    p->encrypted = connInfo.isEncrypted();
                    if (p->encrypted && !p->securedMs)
                        p->securedMs = millis();
                    // The identity address is only meaningful once bonded — both an
                    // iPhone and an Android HU advertise rotating private addresses.
                    strncpy(p->idAddr, connInfo.getIdAddress().toString().c_str(), 17);
                    p->idAddr[17] = 0;
                    if (p->role == Ble::PeerRole::Hid)
                        stageKnownAddr(gPendingHid, gHidAddr, p->idAddr);
                    // NOTE: no Ams branch here. Auth always completes before AMS
                    // discovery can run, so role is still Unknown at this point —
                    // the phone's address is staged from classifyAsAms() instead.
                }
                LOGI("BLE", "auth complete h=%u bonded=%d enc=%d auth=%d bonds=%d",
                     (unsigned)h, connInfo.isBonded(), connInfo.isEncrypted(),
                     connInfo.isAuthenticated(), NimBLEDevice::getNumBonds());
            }
        };

        HubCallbacks gCallbacks;
    } // namespace

    Mode            mode()            { return gMode; }
    NimBLEServer   *server()          { return gServer; }
    Ble::PeerTable &peers()           { return gPeers; }
    uint8_t         connectedCount()  { return gPeers.count(); }
    bool            amsActive()       { return amsActive(gMode); }
    bool            hidActive()       { return hidActive(gMode); }
    const char     *knownAmsAddr()    { return gAmsAddr; }
    const char     *knownHidAddr()    { return gHidAddr; }

    NimBLEClient *bindAmsClient(Ble::Peer &p)
    {
        if (!amsActive() || !gServer)
            return nullptr;
        if (p.clientBound)
            return gServer->getClient(p.handle);
        // Never bind the head unit: getClient() would deleteServices() out from
        // under whatever the AMS peer discovered.
        if (p.role == Ble::PeerRole::Hid || p.role == Ble::PeerRole::NotAms)
            return nullptr;
        if (Ble::addrEq(p.idAddr, gHidAddr))
            return nullptr;
        // The server holds exactly one client. If someone else already has it,
        // this peer cannot be probed for AMS this session.
        if (gPeers.anyClientBound())
            return nullptr;

        NimBLEClient *c = gServer->getClient(p.handle);
        if (!c)
            return nullptr;
        p.clientBound = true;
        LOGI("BLE", "AMS client bound to h=%u %s", (unsigned)p.handle, p.addr);
        return c;
    }

    void classifyAsHid(uint16_t handle, const char *idAddr)
    {
        Ble::Peer *p = gPeers.byHandle(handle);
        if (!p)
            return;
        if (p->clientBound)
        {
            // We speculatively probed this peer for AMS and it turned out to be the
            // head unit. Drop the Apple-side caches; the client object itself stays
            // owned by the server.
            LOGW("BLE", "h=%u was AMS-probed but is the head unit — releasing", (unsigned)handle);
            Bluetooth::OnAmsPeerLost();
            p->clientBound = false;
        }
        p->role = Ble::PeerRole::Hid;
        stageKnownAddr(gPendingHid, gHidAddr, idAddr);

        // Media keys are bursty and latency-tolerant: 100-125ms interval with
        // latency 4 halves this link's radio duty versus the iPhone's parameters,
        // leaving more airtime for WiFi. Worst-case key latency ~250ms is fine.
        if (gServer && !p->paramsRelaxed)
        {
            gServer->updateConnParams(handle, 80, 100, 4, 600);
            p->paramsRelaxed = true;
        }
    }

    uint8_t maxPeers() { return gMode == Mode::Both ? Ble::PeerTable::kMax : 1; }

    void classifyAsAms(uint16_t handle)
    {
        Ble::Peer *p = gPeers.byHandle(handle);
        if (!p)
            return;
        p->role = Ble::PeerRole::Ams;
        stageKnownAddr(gPendingAms, gAmsAddr, p->idAddr);
    }

    void Begin(Mode m, const char *deviceName)
    {
        gMode = m;
        strncpy(gName, deviceName ? deviceName : "MCD1", sizeof(gName) - 1);
        loadKnownAddrs();

        LOGI("BLE", "Begin mode=%s name=%s (known ams=%s hid=%s)",
             m == Mode::Both ? "both" : (m == Mode::Keyboard ? "keyboard" : "ams"),
             gName, gAmsAddr[0] ? gAmsAddr : "-", gHidAddr[0] ? gHidAddr : "-");

        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        NimBLEDevice::init(gName);
        NimBLEDevice::setMTU(247); // so ANCS message bodies fit

        // Bonding ON for every mode. iOS "Just Works" needs it, and HID-over-GATT on
        // Android effectively requires it too — the old keyboard path used
        // bonding=false, which is very likely why it needed re-pairing.
        NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

        gServer = NimBLEDevice::createServer();
        // deleteCallbacks=false — gCallbacks is a file static, not heap.
        gServer->setCallbacks(&gCallbacks, /*deleteCallbacks=*/false);
        gServer->advertiseOnDisconnect(false); // we drive advertising ourselves

        if (hidActive())
            Hid::begin(gServer);

        // Must happen once, after every role's services exist and before advertising.
        gServer->start();

        applyAdvertising(profileForMode());
        LOGI("BLE", "Begin finished (bonds stored: %d)", NimBLEDevice::getNumBonds());
    }

    void Service()
    {
        if (!gServer)
            return;

        // HID first: its deferred key release must not sit behind AMS's GATT work.
        if (hidActive())
            Hid::service();

        if (amsActive())
            Bluetooth::ServiceAms();

        commitStagedAddrs(); // NVS writes happen here, never in a BLE callback

        // Re-advertise whenever a connection slot is free. The old code stopped
        // advertising as soon as the iPhone was up, which in Both mode would make
        // the head unit undiscoverable.
        static uint32_t lastAdv = 0;
        if (gPeers.count() < maxPeers() && millis() - lastAdv > 3000)
        {
            lastAdv = millis();
            NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
            if (adv && !adv->isAdvertising())
                applyAdvertising(profileForMode());
        }
    }
} // namespace BleHub
