#pragma once
// BLE HID consumer-control keyboard, as a *role* on a shared NimBLEServer.
//
// Replaces the old src/BleMediaKeyboard.h, which owned NimBLEDevice::init(),
// createServer() and advertising outright — that is what made it mutually
// exclusive with the AMS path. Here the server is handed in; BleHub owns the
// stack, the advertising payload and the server callbacks.

#include <NimBLEDevice.h>
#include <stdint.h>

// USB HID Consumer Control usage codes
#define KEY_MEDIA_NEXT_TRACK      0x00B5
#define KEY_MEDIA_PREVIOUS_TRACK  0x00B6
#define KEY_MEDIA_PLAY_PAUSE      0x00CD
#define KEY_MEDIA_VOLUME_UP       0x00E9
#define KEY_MEDIA_VOLUME_DOWN     0x00EA

namespace Hid
{
    // Build the HID service tree on `server`. Does NOT start the server and does
    // NOT advertise — BleHub does both, once, after every role is built.
    void begin(NimBLEServer *server);

    // Drains the deferred key release. Must be called from the loop task.
    void service();

    // Queue a consumer-control press. The matching release goes out on the next
    // service() pass: pressing and releasing back-to-back can put both notifications
    // in the same connection event at a relaxed interval, and some head units
    // swallow a zero-duration keypress.
    bool press(uint16_t usageCode);

    bool     connected();
    uint16_t connHandle();

    // Whether the HID tree was built at all (i.e. a HID-capable mode is active).
    bool built();
} // namespace Hid
