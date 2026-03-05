#pragma once
#include <string>
#include <Arduino.h>

namespace Bluetooth {
void Begin(const std::string& device_name);
void End();
void Service();
bool IsConnected();
bool IsTimeSet();
void ClearBonds();

// Status / candidate cycling
const char* GetStatusText();  // e.g. "Scanning...", "1/3 xx:xx", "Connected"
void SelectNext();            // cycle to next discovered AMS device
void SelectPrev();            // cycle to prev discovered AMS device
void ConnectSelected();       // attempt connection to currently selected candidate
void ForgetDevice();          // clear saved preferred address + bonds, restart scan

void SelectByIndex(int idx);   // select candidate by 0-based index and attempt connection

// Returns JSON: {"connected":bool,"status":"...","candidates":[{"name":"...","addr":"...","rssi":N,"type":"0xNN","selected":bool}]}
String GetStatusJson();
}