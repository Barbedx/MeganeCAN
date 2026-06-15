#pragma once
#include <string>
#include <Arduino.h>

// BLE peripheral link to the iPhone. The ESP32 advertises as a connectable
// peripheral named (device_name); the user pairs from iOS Settings > Bluetooth.
// On connect it takes a GATT client over the inbound connection and brings up
// AMS (media) + ANCS (notifications) + CTS (time). Bonding -> silent reconnect.
namespace Bluetooth {

void Begin(const std::string& device_name);
void Service();          // call from loop(): drives setup + ANCS Process()
bool IsConnected();
bool IsTimeSet();
bool HasBond();
void ClearBonds();

const char* GetStatusText(); // short status for the car display
String      GetStatusJson(); // {"connected":bool,"status":"...","bonded":bool,"address":"..."}

} // namespace Bluetooth
