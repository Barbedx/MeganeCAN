#pragma once
// Minimal BLE media keyboard using NimBLE 2.x HIDDevice.
// Replaces the external ESP32-BLE-Keyboard library which is 1.x only.
// Only implements the consumer-control media keys actually used in HandleKey().

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEHIDDevice.h>

// USB HID Consumer Control usage codes
#define KEY_MEDIA_NEXT_TRACK      0x00B5
#define KEY_MEDIA_PREVIOUS_TRACK  0x00B6
#define KEY_MEDIA_PLAY_PAUSE      0x00CD
#define KEY_MEDIA_VOLUME_UP       0x00E9
#define KEY_MEDIA_VOLUME_DOWN     0x00EA

class BleMediaKeyboard : public NimBLEServerCallbacks {
public:
    void begin(const char *name = "BLE Keyboard") {
        NimBLEDevice::init(name);
        NimBLEDevice::setSecurityAuth(/*bonding=*/false, /*mitm=*/false, /*sc=*/true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

        _server = NimBLEDevice::createServer();
        _server->setCallbacks(this);

        _hid = new NimBLEHIDDevice(_server);
        _hid->setManufacturer("gycer");
        _hid->setPnp(0x02, 0x05AC, 0x0239, 0x0110);
        _hid->setHidInfo(0x00, 0x01);

        // Consumer Control descriptor: single 16-bit usage code per report
        static const uint8_t reportMap[] = {
            0x05, 0x0C,        // Usage Page (Consumer)
            0x09, 0x01,        // Usage (Consumer Control)
            0xA1, 0x01,        // Collection (Application)
            0x85, 0x01,        //   Report ID (1)
            0x15, 0x00,        //   Logical Minimum (0)
            0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
            0x75, 0x10,        //   Report Size (16)
            0x95, 0x01,        //   Report Count (1)
            0x19, 0x00,        //   Usage Minimum (0)
            0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
            0x81, 0x00,        //   Input (Data, Array, Absolute)
            0xC0               // End Collection
        };
        _hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));
        _hid->startServices();

        _input = _hid->getInputReport(1);

        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        adv->setAppearance(HID_KEYBOARD);
        adv->addServiceUUID(_hid->getHidService()->getUUID());
        adv->setScanResponseData(NimBLEAdvertisementData()); // no scan response
        adv->start();

        Serial.println("[BLE Kbd] Advertising started");
    }

    bool isConnected() const { return _connected; }

    // Send a consumer-control key press then release
    void write(uint16_t usageCode) {
        if (!_connected || !_input) return;
        uint8_t press[2]   = { (uint8_t)(usageCode & 0xFF), (uint8_t)(usageCode >> 8) };
        uint8_t release[2] = { 0, 0 };
        _input->setValue(press,   sizeof(press));   _input->notify();
        _input->setValue(release, sizeof(release)); _input->notify();
    }

    // NimBLEServerCallbacks
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override {
        _connected = true;
        Serial.println("[BLE Kbd] Connected");
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        _connected = false;
        Serial.println("[BLE Kbd] Disconnected — restarting adv");
        NimBLEDevice::getAdvertising()->start();
    }

private:
    NimBLEServer         *_server    = nullptr;
    NimBLEHIDDevice      *_hid       = nullptr;
    NimBLECharacteristic *_input     = nullptr;
    bool                  _connected = false;
};
