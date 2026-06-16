#include "AuxModeTracker.h"
#include "utils/Log.h"

AuxModeTracker::AuxModeTracker()
  : auxActive(false), lastHeaderTime(0) {
  memset(header, 0, sizeof(header));
}

void AuxModeTracker::onCanMessage(const CAN_FRAME& frame) {
  // Ensure frame is from the expected CAN ID and length is valid
  if (frame.id != 0x151 || frame.length < 8) return;

  uint8_t firstByte = frame.data.uint8[0];
  
  if (firstByte == 0x10) {
    // Save header values from 0x10 frame
    memcpy(header, frame.data.uint8, 8);
    lastHeaderTime = millis();
  } else if (firstByte == 0x21) {
    // Check if a recent header was received
    if (millis() - lastHeaderTime < 200) {
      bool nowAux = isAux(header, frame.data.uint8);
      if (nowAux != auxActive) {
        auxActive = nowAux;
        LOGI("AUX", "AUX mode changed: %s", auxActive ? "ENTERED" : "EXITED");
      }
    }
  }
}

bool AuxModeTracker::isAux(const uint8_t* head, const uint8_t* text) {
    // Debug: dump header + text frame bytes (hex + as characters)
    LOGT("AUX", "isAux: Header bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
         head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7]);
    LOGT("AUX", "isAux: Text bytes: '%c' '%c' '%c' '%c' '%c' '%c' '%c' '%c'",
         (char)text[0], (char)text[1], (char)text[2], (char)text[3],
         (char)text[4], (char)text[5], (char)text[6], (char)text[7]);
    LOGT("AUX", "isAux: Text bytes in HEX: %02X %02X %02X %02X %02X %02X %02X %02X",
         text[0], text[1], text[2], text[3], text[4], text[5], text[6], text[7]);

    // Check if the text clearly indicates AUX (just the 3 letters, trailing content varies by radio)
    if (text[1] == 'A' && text[2] == 'U' && text[3] == 'X') {
        LOGD("AUX", "Definitely AUX");
        return true;
    }
    // Check if the text clearly indicates RENAULT (radio/normal mode)
    if (text[1] == 'R' && text[2] == 'E' && text[3] == 'N'
    && text[4] == 'A'
    && text[5] == 'U'
    && text[6] == 'L'
    && text[7] == 'T')
    {
        LOGD("AUX", "Definitely Radio (RENAULT)");
        return false;
    }

    // Check for CD mode (TR [0-9] CD [0-9])
    if (text[1] == 'T' && text[2] == 'R' && text[3] == ' ' &&
        (text[4] == ' ' || isDigit(text[4])) &&
        isDigit(text[5]) && text[6] == ' ' && text[7] == 'C') {
        LOGD("AUX", "Definitely CD mode");
        return false;
    }

    // Check for radio mode (starting with '> ' and header byte 7 >= 0x59)
    if (text[1] == '>' && text[2] == ' ' && text[3] != ' ' &&
        head[6] >= 0x59) {
        LOGD("AUX", "Definitely Radio mode (short)");
        return false;
    }

    // Check for radio mode with 'M' (and digits after it)
    if (text[1] == 'M' && text[2] == ' ' &&
        (text[3] == ' ' || isDigit(text[3])) &&
        isDigit(text[4]) && isDigit(text[5]) &&
        isDigit(text[6]) && text[7] == ' ') {
        LOGD("AUX", "Definitely Radio M mode");
        return false;
    }

    // Check for radio mode with 'L' (and digits after it)
    if (text[1] == 'L' && text[2] == ' ' &&
        text[3] == ' ' && isDigit(text[4]) &&
        isDigit(text[5]) && isDigit(text[6]) &&
        text[7] == ' ') {
        LOGD("AUX", "Definitely Radio L mode");
        return false;
    }

    // Check for a pattern indicating Radio mode 1: "   1056" (spaces and digits)
    if (text[1] == ' ' && text[2] == ' ' && text[3] == ' ' &&
        isDigit(text[4]) && isDigit(text[5]) &&
        isDigit(text[6]) && isDigit(text[7]) && head[6] < 0x59) {
        LOGD("AUX", "Definitely Radio Mode 1");
        return false;
    }

    // If no clear mode, retain the last known state
    LOGD("AUX", "Undetermined mode - retaining last state.");
    return auxActive;
}

bool AuxModeTracker::isInAuxMode() const {
  return auxActive;
}

void AuxModeTracker::SetAuxMode(bool value) 
{
  auxActive =   value;
}