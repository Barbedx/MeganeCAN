#include "bluetooth.h"
#include "utils/Log.h"
#include "ble/BleHub.h"
#include "ble/BlePeerTable.h"
#include "ble/HidRole.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>

#include "apple_media_service.h"
#include "apple_notification_service.h"
#include "current_time_service.h"

namespace Bluetooth
{
    namespace
    {
        NimBLEClient *Client = nullptr; // GATT client bound to the AMS peer

        volatile bool Connected = false; // AMS is up
        volatile bool Secured   = false; // link encrypted/bonded
        RTC_DATA_ATTR bool TimeSet = false;

        // Each Apple service is brought up independently and retried until it
        // appears (iOS exposes AMS/ANCS/CTS with slightly different timing).
        bool AmsUp = false, AncsUp = false, CtsUp = false;

        // How long we keep probing for AMS *after the link is encrypted*. Measuring
        // from encryption rather than from connect is deliberate: the unencrypted
        // stretch is the user walking over and tapping "Pair", which can take tens of
        // seconds and must not count against the probe budget.
        const uint32_t SETUP_DEADLINE_MS = 20000;

        // How long we wait for an unclassified peer to encrypt before giving up on
        // it. In Both mode a peer that never pairs would otherwise hold the single
        // GATT client hostage while the real phone waits for a slot.
        const uint32_t SECURE_WAIT_MS = 45000;

        // Re-initiate pairing at most this often, and only this many times.
        const uint32_t SECURE_RETRY_MS  = 4000;
        const uint8_t  SECURE_MAX_TRIES = 6;
        uint32_t lastSecureAttempt = 0;

        char        PeerAddr[18] = {0};
        const char *StatusText   = "Pair from iPhone";
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

        void resetAmsState()
        {
            Connected = false;
            Secured   = false;
            AmsUp = AncsUp = CtsUp = false;
            Client = nullptr;
            PeerAddr[0] = 0;
        }
    } // namespace

    void OnAmsPeerLost()
    {
        resetAmsState();
        // These cache NimBLERemoteCharacteristic pointers owned by the client we
        // just lost. NimBLEServer::getClient() calls deleteServices() when it is
        // rebound to another peer, so leaving them set is a use-after-free, not a
        // stale-value bug.
        AppleMediaService::Detach();
        AppleNotificationService::Detach();
        CurrentTimeService::StopTimeService();
        StatusText = HasBond() ? "Waiting for phone" : "Pair from iPhone";
        LOGI("BT", "AMS peer lost — caches detached");
    }

    void ServiceAms()
    {
        // Drive deferred ANCS Control Point writes from this (loop) task.
        if (Connected && Client && Client->isConnected())
            AppleNotificationService::Process();

        if (millis() - lastSetupAttempt < 800)
            return;
        lastSetupAttempt = millis();

        // Find a peer worth probing: the known iPhone, or an unclassified newcomer.
        Ble::PeerTable &peers = BleHub::peers();
        Ble::Peer *target = peers.byRole(Ble::PeerRole::Ams);
        if (!target)
            target = peers.byRole(Ble::PeerRole::Unknown);

        if (!target)
        {
            if (Connected || Client)
                OnAmsPeerLost();
            return;
        }

        // Everything below can block on GATT, during which the host task may drop
        // this peer and hand its table slot to a different device. Re-fetch by handle
        // after each blocking step instead of trusting the pointer.
        const uint16_t h = target->handle;

        if (AmsUp && AncsUp && CtsUp)
            return; // fully up

        if (!Client)
        {
            Client = BleHub::bindAmsClient(*target);
            if (!Client)
                return; // not eligible (head unit, or the single client is taken)
            strncpy(PeerAddr, target->addr, sizeof(PeerAddr) - 1);
            StatusText = "Connecting...";
        }
        if (!Client->isConnected())
            return;

        // Secure first, then discover (order matters — NimBLE issue #1033).
        //
        // ASYNC on purpose. secureConnection(false) does taskWait(BLE_NPL_TIME_FOREVER)
        // — it blocks the calling task with no timeout until the user taps "Pair".
        // On the loop task that stalls the display, WiFi, and (in Both mode) the
        // deferred HID key release, so a held volume key would run away to max.
        // Async kicks off pairing and returns; encryption is observed via the peer's
        // `encrypted` flag, which onAuthenticationComplete sets.
        Secured = target->encrypted;
        if (!Secured)
        {
            if (millis() - target->connectedMs > SECURE_WAIT_MS)
            {
                LOGW("BT", "h=%u never encrypted after %us — not the phone",
                     (unsigned)h, (unsigned)(SECURE_WAIT_MS / 1000));
                target->role = Ble::PeerRole::NotAms;
                target->clientBound = false;
                OnAmsPeerLost();
                return;
            }
            if (target->secureTries < SECURE_MAX_TRIES &&
                millis() - lastSecureAttempt > SECURE_RETRY_MS)
            {
                lastSecureAttempt = millis();
                target->secureTries++;
                LOGI("BT", "Initiating pairing (try %u) — accept the prompt on your iPhone",
                     (unsigned)target->secureTries);
                Client->secureConnection(/*async=*/true);
            }
            return;
        }

        // Past this point the link is encrypted; the probe budget starts here.
        const uint32_t securedAt = target->securedMs ? target->securedMs : target->connectedMs;
        const bool     expired   = millis() - securedAt > SETUP_DEADLINE_MS;

        if (!AmsUp && expired)
        {
            LOGI("BT", "h=%u exposed no AMS %us after encrypting — marking not-ams",
                 (unsigned)h, (unsigned)(SETUP_DEADLINE_MS / 1000));
            target->role        = Ble::PeerRole::NotAms;
            target->clientBound = false;
            OnAmsPeerLost();
            return;
        }
        // AMS up but a straggler never appeared (iOS sometimes never exposes ANCS).
        // Stop retrying, or we re-run GATT discovery every 800ms forever.
        if (AmsUp && expired)
            return;

        // Bring up each service independently and keep retrying the missing ones.
        if (!AmsUp && AppleMediaService::StartMediaService(Client))
        {
            // The peer may have gone during discovery — validate before writing to it.
            if (!BleHub::peers().byHandle(h))
            {
                OnAmsPeerLost();
                return;
            }
            AmsUp     = true;
            Connected = true;
            BleHub::classifyAsAms(h); // AMS present == this is the phone; remember it
            StatusText = "Connected";
            LOGI("BT", "AMS started");
        }
        if (!BleHub::peers().byHandle(h))
        {
            OnAmsPeerLost();
            return;
        }
        if (AmsUp && !AncsUp && AppleNotificationService::StartNotificationService(Client))
        {
            AncsUp = true;
            LOGI("BT", "ANCS started");
        }
        if (AmsUp && !CtsUp && startCTS(Client))
        {
            CtsUp = true;
            LOGI("BT", "CTS started");
        }
    }

    bool IsConnected() { return Connected && Client && Client->isConnected(); }
    bool IsTimeSet()   { return TimeSet; }
    bool HasBond()     { return NimBLEDevice::getNumBonds() > 0; }

    void ClearBonds()
    {
        LOGI("BT", "Clearing all bonds (this also drops the head unit's bond)...");
        NimBLEDevice::deleteAllBonds();
        if (Client && Client->isConnected())
            Client->disconnect();
        // Release the client ownership flag too. Leaving it set made bindAmsClient()
        // hand back the same client forever while ServiceAms sat in the expired
        // branch doing nothing — AMS was dead until reboot.
        for (uint8_t i = 0; i < Ble::PeerTable::kMax; i++)
            if (Ble::Peer *p = BleHub::peers().at(i))
                p->clientBound = false;
        OnAmsPeerLost();
        StatusText = "Pair from iPhone";
        LOGI("BT", "Bonds cleared. Also 'Forget this device' on the iPhone, then re-pair.");
    }

    const char *GetStatusText() { return StatusText; }

    String GetStatusJson()
    {
        const bool amsOn = BleHub::amsActive();
        const bool hidOn = BleHub::hidActive();
        const bool hidUp = hidOn && Hid::connected();

        Ble::Peer *hidPeer = BleHub::peers().byRole(Ble::PeerRole::Hid);

        char buf[360];
        // Legacy top-level keys mirror the AMS link so existing callers (dashboard,
        // Carminat now-playing) keep working unchanged.
        snprintf(buf, sizeof(buf),
                 "{\"mode\":\"%s\""
                 ",\"connected\":%s,\"status\":\"%s\",\"bonded\":%s,\"address\":\"%s\""
                 ",\"ams\":{\"active\":%s,\"connected\":%s,\"secured\":%s,\"address\":\"%s\""
                 ",\"svc\":{\"ams\":%s,\"ancs\":%s,\"cts\":%s}}"
                 ",\"hid\":{\"active\":%s,\"connected\":%s,\"address\":\"%s\"}}",
                 BleHub::mode() == BleHub::Mode::Both ? "both"
                     : (BleHub::mode() == BleHub::Mode::Keyboard ? "keyboard" : "ams"),
                 IsConnected() ? "true" : "false", StatusText,
                 HasBond() ? "true" : "false", IsConnected() ? PeerAddr : "",
                 amsOn ? "true" : "false", IsConnected() ? "true" : "false",
                 Secured ? "true" : "false", IsConnected() ? PeerAddr : "",
                 AmsUp ? "true" : "false", AncsUp ? "true" : "false", CtsUp ? "true" : "false",
                 hidOn ? "true" : "false", hidUp ? "true" : "false",
                 hidPeer ? hidPeer->addr : "");
        return String(buf);
    }
} // namespace Bluetooth
