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
      Serial.print("'");
      Serial.print((char)text[i]);
      Serial.print("' ");
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
  
    // Check the text frame bytes exactly.
    // If bytes at indexes 1-3 form "AUX", then we are sure it's AUX.
    if (text[1] == 'A' && text[2] == 'U' && text[3] == 'X')
    {
        Serial.println("Definettely AUX");

        return true;
    }
 
    

    if (text[1] == 'T' && 
        text[2] == 'R' && 
        text[3] == ' ' && 
        (text[4]== ' '  || isDigit(text[4]) ) && 
        isDigit(text[5]) && 
        text[6] == ' ' && 
        text[7] == 'C'         
    )
    {
        Serial.println("Definettely CD mode");

        return false;
    }
    
    if (text[1] == '>' && 
        text[2] == ' ' && 
        text[3] != ' '  //for radio station? meaybe check also 22 frame(itwill contains"<")        
        && head[6] >=0x59 
    )
    {
        Serial.println("Definettely radio short mode");

        return false;
    }
    
    if (text[1] == 'M' && 
        text[2] == ' ' && 
        (text[3]== ' '  || isDigit(text[3]) ) && 
        isDigit(text[4]) &&   //for radio station? meaybe check also 22 frame(itwill contains"<")        
        isDigit(text[5]) &&   //for radio station? meaybe check also 22 frame(itwill contains"<")        
        isDigit(text[6]) &&   //for radio station? meaybe check also 22 frame(itwill contains"<")        
        text[7] == ' '  //for radio station? meaybe check also 22 frame(itwill contains"<")        
    )
    {
        Serial.println("Definettely radio M mode");

        return false;
    }
    if (text[1] == ' ' && 
        text[2] == ' ' && 
        text[3]== ' '  &&

        isDigit(text[4]) &&   //for radio station? meaybe check also 22 frame(itwill contains"<")        
        isDigit(text[5]) &&   //for radio station? meaybe check also 22 frame(itwill contains"<")        
        isDigit(text[6]) &&   //for radio station? meaybe check also 22 frame(itwill contains"<")        
        isDigit(text[6]) 
        && head[6]<0x59
        //for radio station? meaybe check also 22 frame(itwill contains"<")        
    )
    {
        Serial.println("Definettely radio M mode");

        return false;
    }
    
    if (text[1] == 'L' && 
        text[2] == ' ' && 
        text[3]== ' '  && 
        isDigit(text[4]) &&   //for radio station? meaybe check also 22 frame(itwill contains"<")        
        isDigit(text[5]) &&   //for radio station? meaybe check also 22 frame(itwill contains"<")        
        isDigit(text[6]) &&   //for radio station? meaybe check also 22 frame(itwill contains"<")        
        text[7] == ' '  //for radio station? meaybe check also 22 frame(itwill contains"<")        
    )
    {
        Serial.println("Definettely radio L mode");

        return false;
    }
    
     return auxActive;
  }
  

bool AuxModeTracker::isInAuxMode() const {
  return auxActive;
}
