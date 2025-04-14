#ifndef AUX_MODE_TRACKER_H
#define AUX_MODE_TRACKER_H

#include <Arduino.h>
#include <string.h>
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */

class AuxModeTracker {
public:
  AuxModeTracker();

  // Call this for each incoming CAN frame
  void onCanMessage(const CAN_FRAME& frame);

  // Check if AUX mode is active
  bool isInAuxMode() const;

private:
  bool auxActive;
  uint8_t header[8]; // Store the most recent 0x10 frame data
  unsigned long lastHeaderTime;

  // Internal helper to check AUX criteria
  bool isAux(const uint8_t* head, const uint8_t* text);
};

#endif // AUX_MODE_TRACKER_H