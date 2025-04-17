#include "AuxModeTracker.h"

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
        Serial.print("AUX mode changed: ");
        Serial.println(auxActive ? "ENTERED" : "EXITED");
      }
    }
  }
}

bool AuxModeTracker::isAux(const uint8_t* head, const uint8_t* text) {
    // Debug: print header bytes
    Serial.print("isAux: Header bytes: ");
    for (int i = 0; i < 8; i++) {
      Serial.print("0x");
      if(head[i] < 0x10) Serial.print("0");
      Serial.print(head[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
  
    // Debug: print text frame bytes (as characters and as hex)
    Serial.print("isAux: Text bytes: ");
    for (int i = 0; i < 8; i++) {
      Serial.print("'"); Serial.print((char)text[i]); Serial.print("' ");
    }
    Serial.println();
    Serial.print("isAux: Text bytes in HEX: ");
    for (int i = 0; i < 8; i++) {
      Serial.print("0x");
      if(text[i] < 0x10) Serial.print("0");
      Serial.print(text[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
  
    // Check if the text clearly indicates AUX
    if (text[1] == 'A' && text[2] == 'U' && text[3] == 'X'
    && text[4] == ' '
    && text[5] == ' '
    && text[6] == ' '
    && text[7] == ' ' 
    
    ) {
        Serial.println("Definitely AUX");
        return true;
    }

    // Check for CD mode (TR [0-9] CD [0-9])
    if (text[1] == 'T' && text[2] == 'R' && text[3] == ' ' &&
        (text[4] == ' ' || isDigit(text[4])) && 
        isDigit(text[5]) && text[6] == ' ' && text[7] == 'C') {
        Serial.println("Definitely CD mode");
        return false;
    }

    // Check for radio mode (starting with '> ' and header byte 7 >= 0x59)
    if (text[1] == '>' && text[2] == ' ' && text[3] != ' ' &&
        head[6] >= 0x59) {
        Serial.println("Definitely Radio mode (short)");
        return false;
    }

    // Check for radio mode with 'M' (and digits after it)
    if (text[1] == 'M' && text[2] == ' ' &&
        (text[3] == ' ' || isDigit(text[3])) &&
        isDigit(text[4]) && isDigit(text[5]) &&
        isDigit(text[6]) && text[7] == ' ') {
        Serial.println("Definitely Radio M mode");
        return false;
    }

    // Check for radio mode with 'L' (and digits after it)
    if (text[1] == 'L' && text[2] == ' ' &&
        text[3] == ' ' && isDigit(text[4]) &&
        isDigit(text[5]) && isDigit(text[6]) &&
        text[7] == ' ') {
        Serial.println("Definitely Radio L mode");
        return false;
    }

    // Check for a pattern indicating Radio mode 1: "   1056" (spaces and digits)
    if (text[1] == ' ' && text[2] == ' ' && text[3] == ' ' &&
        isDigit(text[4]) && isDigit(text[5]) &&
        isDigit(text[6]) && isDigit(text[7]) && head[6] < 0x59) {
        Serial.println("Definitely Radio Mode 1");
        return false;
    }

    // If no clear mode, retain the last known state
    Serial.println("Undetermined mode - retaining last state.");
    return auxActive;
}

bool AuxModeTracker::isInAuxMode() const {
  return auxActive;
}

void AuxModeTracker::SetAuxMode(bool value) 
{
  auxActive =   value;
}