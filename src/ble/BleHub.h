#pragma once
// Sole owner of the BLE stack: NimBLEDevice::init, the security config, the one
// NimBLEServer, the one NimBLEServerCallbacks, the peer table and the advertising
// payload. Roles (AMS client, HID server) plug into it.
//
// Before this existed, both Bluetooth::Begin() and BleMediaKeyboard::begin() called
// NimBLEDevice::init() + createServer(). Those calls are internally idempotent, so
// the second one did not fail loudly — it silently discarded the second device name
// and let setSecurityAuth/setCallbacks be last-writer-wins. That is why the two
// modes could never coexist.

#include <NimBLEDevice.h>
#include "BlePeerTable.h"

namespace BleHub
{
    enum class Mode : uint8_t
    {
        Ams,      // iPhone only (legacy "ams")
        Keyboard, // head unit only (legacy "keyboard")
        Both,     // iPhone AMS + head-unit HID simultaneously
    };

    void Begin(Mode m, const char *deviceName);

    // Pumps both roles. Call from the loop task only — never from a BLE callback.
    void Service();

    Mode            mode();
    NimBLEServer   *server();
    Ble::PeerTable &peers();
    uint8_t         connectedCount();

    inline bool amsActive(Mode m) { return m == Mode::Ams || m == Mode::Both; }
    inline bool hidActive(Mode m) { return m == Mode::Keyboard || m == Mode::Both; }
    bool amsActive();
    bool hidActive();

    // Called by the HID role when a peer subscribes to the input report. Marks it
    // as the head unit, remembers its identity address in NVS, and relaxes that
    // link further than the iPhone's (key traffic is bursty and latency-tolerant).
    void classifyAsHid(uint16_t handle, const char *idAddr);

    // Called from the loop task once AMS is confirmed present on a peer. Splitting
    // this out of onAuthenticationComplete matters: auth completes strictly BEFORE
    // AMS discovery can run, so a save gated on "role == Ams" inside that callback
    // could never fire and the phone's address was never learned.
    void classifyAsAms(uint16_t handle);

    // How many simultaneous links this mode wants. 1 for the single-role modes, so
    // they stop advertising once their peer is up (legacy behaviour) and no stray
    // central can take a second slot.
    uint8_t maxPeers();

    // The single gate on NimBLEServer::getClient(). That call keeps ONE client for
    // the whole server and calls deleteServices() when rebound, which frees every
    // NimBLERemoteCharacteristic the Apple services cached. Binding it to the wrong
    // peer is heap corruption, not a lost pointer. Every getClient() call site must
    // go through here.
    NimBLEClient *bindAmsClient(Ble::Peer &p);

    // Identity address of the peer we last classified, "" if never seen.
    const char *knownAmsAddr();
    const char *knownHidAddr();
} // namespace BleHub
