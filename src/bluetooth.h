#pragma once
#include <string>
#include <Arduino.h>

// The AMS role: our GATT *client* onto the iPhone, running over the inbound
// connection the phone made to us (we are the GAP peripheral, the phone is the
// GATT server for AMS/ANCS/CTS). Bonding -> silent reconnect.
//
// The BLE stack itself — init, security, the NimBLEServer, advertising, the peer
// table — belongs to BleHub. This namespace no longer owns any of that; it is
// driven by BleHub::Service(). Do NOT call NimBLEServer::getClient() from here;
// go through BleHub::bindAmsClient(), which enforces the one-client invariant.
namespace Bluetooth {

// --- driven by BleHub ---
void ServiceAms();     // per-loop AMS setup/keepalive; called by BleHub::Service()
void OnAmsPeerLost();  // the peer holding our client went away — drop all caches

// --- public status surface (unchanged callers) ---
bool IsConnected();
bool IsTimeSet();
bool HasBond();
void ClearBonds();

const char* GetStatusText(); // short status for the car display
String      GetStatusJson(); // legacy keys + nested "ams"/"hid" objects

} // namespace Bluetooth
