#include "Affa3Display.h"
#include <Arduino.h>

#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
namespace Affa3Display {




    void sendCan(uint32_t id, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
        uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7) {
      CAN_FRAME frame;
      frame.id = id;
      frame.length = 8;
      frame.data.uint8[0] = d0;
      frame.data.uint8[1] = d1;
      frame.data.uint8[2] = d2;
      frame.data.uint8[3] = d3;
      frame.data.uint8[4] = d4;
      frame.data.uint8[5] = d5;
      frame.data.uint8[6] = d6;
      frame.data.uint8[7] = d7;
      
      CAN0.sendFrame(frame);
      }
      
    void emulateKey(uint16_t key, bool hold) {
        CAN_FRAME frame;
        frame.id = 0x1C1; // ID expected by gotFrame_0x1C1()
        frame.length = 8;
        frame.extended = 0; // standard frame
      
        frame.data.uint8[0] = 0x03; // must be 0x03
        frame.data.uint8[1] = 0x89; // must be 0x89
        frame.data.uint8[2] = (key >> 8) & 0xFF; // key high byte
        frame.data.uint8[3] = key & 0xFF;        // key low byte
      
        // Fill the rest with 0 or whatever is standard
        frame.data.uint8[4] = 0;
        frame.data.uint8[5] = 0;
        frame.data.uint8[6] = 0;
        frame.data.uint8[7] = 0;
      
        if (hold) {
          frame.data.uint8[3] |= AFFA3_KEY_HOLD_MASK;
        }
        
        CAN0.sendFrame(frame);
      
        Serial.print("Emulated key press: 0x");
        Serial.println(key, HEX);
      }

void showMenu(
    const char* header, 
    const char* item1, 
    const char* item2, 
    uint8_t firstFrameSize, 
    uint8_t scrollLockIndicator,
    uint8_t selectionItem1, 
    uint8_t selectionItem2
  ) {
    Serial.println("[showMenu] --- Building Menu ---");
  
    Serial.print("[Header] "); Serial.println(header);
    Serial.print("[Item1] "); Serial.println(item1);
    Serial.print("[Item2] "); Serial.println(item2);
  
    uint8_t payload[96] = {0};
    int idx = 0;
  
    payload[idx++] = 0x21;
    payload[idx++] = 0x01;
    payload[idx++] = 0x7E;
    payload[idx++] = 0x80;
    payload[idx++] = 0x00;
    payload[idx++] = 0x00;
  
    Serial.print("[CAN] Sending First Frame: ID=0x151 Data= ");
    Serial.printf("10 %02X ", firstFrameSize);
    for (int i = 0; i < 6; i++) Serial.printf("%02X ", payload[i]);
    Serial.println();
  
    sendCan(0x151, 0x10, firstFrameSize, payload[0], payload[1], payload[2], payload[3], payload[4], payload[5]);
  
    payload[idx++] = 0x82;
    payload[idx++] = 0xFF;
    payload[idx++] = scrollLockIndicator;
  
    int maxHeaderLen = 26;
    int h = 0;
    while (header && header[h] && h < maxHeaderLen && idx < 96) {
      payload[idx++] = header[h++];
    }
  
    while (idx < 35) payload[idx++] = 0x00;
  
    payload[idx++] = selectionItem1;
    payload[idx++] = 0x7E;
    for (int i = 0; i < 4; i++) payload[idx++] = (i < strlen(item1)) ? item1[i] : ' ';
    while (idx < 62) payload[idx++] = 0x00;
  
    payload[idx++] = selectionItem2;
    payload[idx++] = 0x7F;
    while (*item2 && idx < 96) payload[idx++] = *item2++;
    while (idx < 96) payload[idx++] = 0x00;
  
    uint8_t seq = 1;
    for (int i = 6; i < idx; i += 7) {
      uint8_t d[8];
      d[0] = 0x20 | (seq++ & 0x0F);
      for (int j = 0; j < 7; j++) {
        d[j + 1] = (i + j < idx) ? payload[i + j] : 0x00;
      }
  
      sendCan(0x151, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
      delay(5);
    }
  
    Serial.println("[showMenu] --- Done ---");
  }
  
  void showInfoMenu(
    const char* item1,
    const char* item2,
    const char* item3,
    uint8_t offset1,
    uint8_t offset2,
    uint8_t offset3, 
    uint8_t infoPrefix
  ) {
    Serial.println("[showInfoMenu] --- Sending Info Menu ---");
  
    auto sendMenuItem = [&](uint8_t offset, const char* text, const char* label) {
      char padded[8] = { ' ' };
      strncpy(padded, text, 8);
  
      Serial.print("[MenuItem] "); Serial.print(label);
      Serial.print(" | Offset: 0x"); Serial.print(offset, HEX);
      Serial.print(" | Text: \""); Serial.print(padded); Serial.println("\"");
  
      sendCan(0x151, 0x10, 0x0B, 0x76, infoPrefix, offset, padded[0], padded[1], padded[2]);
      delay(5);
      sendCan(0x151, 0x21, padded[3], padded[4], padded[5], padded[6], padded[7], 0x00, 0x00);
      delay(5);
    };
  
    sendMenuItem(offset1, item1, "Item1");
    sendMenuItem(offset2, item2, "Item2");
    sendMenuItem(offset3, item3, "Item3");
  
    Serial.println("[showInfoMenu] --- Done ---");
  }

  
void setTime(char* clock) { 
    CAN_FRAME answer;
    answer.id = 0x151;
    answer.length = 8;
    answer.data.uint8[0] = 0x05;
    answer.data.uint8[1] = 'V';              // likely constant
    answer.data.uint8[2] = clock[0];
    answer.data.uint8[3] = clock[1];
    answer.data.uint8[4] = clock[2];
    answer.data.uint8[5] = clock[3];
    answer.data.uint8[6] = 0x00;
    answer.data.uint8[7] = 0x00;
  
    CAN0.sendFrame(answer);
    Serial.print("Sent time set frame with year: ");
    Serial.println(clock);
  }

  
void sendDisplayFrame(uint8_t frameIndex, const char* textSegment) {
    CAN_FRAME frame;
    frame.id = 0x151;
    frame.length = 8;
    frame.data.uint8[0] = 0x21 + frameIndex; // 0x21, 0x22, ...
    
    for (int i = 0; i < 7; i++) {
      frame.data.uint8[i + 1] = textSegment[i];
    }
  
    CAN0.sendFrame(frame);
    Serial.println("Header sended:" );
   // printFrame_inline(frame); 
    delay(5);
  }
  
void display_Control( int8_t state){

    if(state == 1 )
    sendCan(151,3,	52,		9,0,	0	,0,	0,	0);//sc 151 3 52 9 0 0 0 0 0
    
    else   
    sendCan(151,3,	52,		0,0,	0	,0,	0,	0);//sc 151 3 52 9 0 0 0 0 0
    
  }
  
  void sendDisplayHeader(
    uint8_t mode,            // byte3 - 74: full, 77: partial
    uint8_t rdsIcon,         // byte4 - 45: AF-RDS, 55: none
    uint8_t unknown,         // byte5 - usually 55
    uint8_t sourceIcon,      // byte6 - df: MANU, fd: PRESET, ff: NONE
    uint8_t textFormat,      // byte7 - 19–3F: radio, 59–7F: plain
    uint8_t controlByte      // byte8 - always 1
  ) {
    CAN_FRAME frame;
    frame.id = 0x151;
    frame.length = 8;
  
    frame.data.uint8[0] = 0x10; // First ISO-TP frame
    frame.data.uint8[1] = 0x0E; // Total length (14 bytes)
  
    frame.data.uint8[2] = mode;
    frame.data.uint8[3] = rdsIcon;
    frame.data.uint8[4] = unknown;
    frame.data.uint8[5] = sourceIcon;
    frame.data.uint8[6] = textFormat;
    frame.data.uint8[7] = controlByte;
  
    CAN0.sendFrame(frame);
    Serial.println("Header sended:" );
    //printFrame_inline(frame);
    delay(5);
  }
  /**
   * Sends a text string to the car display over CAN bus.
   *
   * @param mode Display mode: 
   *             0x74 - full window,
   *             0x77 - partial window (some values freeze UI if not matched)
   *
   * @param rdsIcon RDS icon display:
   *                0x45 - AF-RDS icon,
   *                0x55 - no icon
   *
   * @param sourceIcon Source label icon:
   *                   0xDF - "MANU",
   *                   0xFD - "PRESET",
   *                   0xFF - none,
   *                   others show icons like "LIST"
   *
   * @param channel Text display format:
   *                   0x19–0x3F - Radio-style (5 digits + '.' + 1 char),
   *                   0x59–0x7F - Plain ASCII (up to 7 chars visible)
   *                   channel symbol
   *
   * @param controlByte Always 0x01 — required by display protocol
   *
   * @param rawText Text to display (max 7 characters shown).
   *                Underscores (_) are replaced with spaces.
   */
  void sendTextToDisplay(
    uint8_t mode,
    uint8_t rdsIcon,
    uint8_t sourceIcon,
    uint8_t channel,
    
    const char* rawText
  ) {
    // Convert and pad text to 7 characters only
    String text = String(rawText);
    text.replace("_", " ");
    while (text.length() < 8) text += ' ';
  
    sendDisplayHeader(mode, rdsIcon, 0x55/*unknown*/, sourceIcon, channel, 1);
    sendDisplayFrame(0, text.c_str());  //  0x21
    delay(50);
    sendDisplayFrame(1, text.c_str()+7);  //  0x22 (send next frame wit [7..16])
  }
  void scrollTextRightToLeft(
    uint8_t mode,
    uint8_t rdsIcon,
    uint8_t sourceIcon,
    uint8_t channel,
    const char* rawText,
    int speed ,
    int count ,
    const char* padding  // 8 spaces
  ) {
    // Step 1: Clean input
    String cleanedText = String(rawText);
    cleanedText.replace("_", " "); // Replace underscores with spaces
  
    // Step 2: Build the full scrolling text
    String fullText ="        ";// String(padding);  // Leading padding always 8
  
    for (int i = 0; i < count; i++) {
      fullText += cleanedText + padding;  // Append text + padding each time
    }
  
    // Add extra safety padding to prevent out-of-bounds
    fullText += padding; //TODO: Fix that
  
    int totalLength = fullText.length() - 7; // Scroll window of 8 chars
  
    Serial.println("---- scrollTextRightToLeft ----");
    Serial.print("Raw text: ");
    Serial.println(rawText);
    Serial.print("Full text: ");
    Serial.println(fullText);
    Serial.print("Speed: ");
    Serial.println(speed);
    Serial.print("Total scroll steps: ");
    Serial.println(totalLength);
  
    // Step 3: Scroll the text
    for (int i = 0; i < totalLength; i++) {
      String segment = fullText.substring(i, i + 8);
  
      // Pad just in case it's short
      while (segment.length() < 8) {
        segment += " ";
      }
  
      Serial.print("Segment [");
      Serial.print(i);
      Serial.print("]: ");
      Serial.println(segment);
  
      sendTextToDisplay(mode, rdsIcon, sourceIcon, channel, segment.c_str());
      delay(speed);
    }
  
    Serial.println("---- scrollTextRightToLeft done ----");
  }
}