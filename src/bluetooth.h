#pragma once
#include <string>

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
}