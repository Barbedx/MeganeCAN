#include <Arduino.h>

#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
#include "AuxModeTracker.h"
 
#include <SerialCommand.h> 

#include <SerialCommand.h> // Assuming this is already included in your project

#include <BleKeyboard.h>

// Replace with your network credentials 


// Web server running in AP mode 


// Create an AsyncWebServer object on port 80
//AsyncWebServer server(80);


//for menu navigation
// sc 151 7 29 1 7F 80 0 0 0
// sc 151 7 29 1 7E 80 0 0 0










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
#define AFFA3_PACKET_LEN 0x08
#define AFFA2_KEY_LOAD 0x0000 /* This at the bottom of the remote;) */
#define AFFA2_KEY_SRC_RIGHT 0x0001
#define AFFA2_KEY_SRC_LEFT 0x0002
#define AFFA2_KEY_VOLUME_UP 0x0003
#define AFFA2_KEY_VOLUME_DOWN 0x0004
#define AFFA2_KEY_PAUSE 0x0005
#define AFFA2_KEY_ROLL_UP 0x0101
#define AFFA2_KEY_ROLL_DOWN 0x0141
#define AFFA2_KEY_HOLD_MASK (0x80 | 0x40)
BleKeyboard bleKeyboard("Bluetooth Device Name", "Bluetooth Device Manufacturer", 100);

bool current_in_AUX_mode = false; // Global variable for AUX mode tracking
void unrecognized(const char *command); 

void showMenu(
  const char* header, 
  const char* item1, 
  const char* item2, 
  uint8_t firstFrameSize = 0x5A, 
  uint8_t scrollLockIndicator = 0x0B,
  uint8_t selectionItem1 = 0x00, 
  uint8_t selectionItem2 = 0x01
) {
  Serial.println("[showMenu] --- Building Menu ---");

  Serial.print("[Header] "); Serial.println(header);
  Serial.print("[Item1] "); Serial.println(item1);
  Serial.print("[Item2] "); Serial.println(item2);

  uint8_t payload[96] = {0};
  int idx = 0;

  // Initial protocol fields
  payload[idx++] = 0x21;
  payload[idx++] = 0x01;
  payload[idx++] = 0x7E;
  payload[idx++] = 0x80;
  payload[idx++] = 0x00;
  payload[idx++] = 0x00;
// Send First Frame
Serial.print("[CAN] Sending First Frame: ID=0x151 Data= ");
Serial.printf("10 %02X ", firstFrameSize);
for (int i = 0; i < 6; i++) {
  Serial.printf("%02X ", payload[i]);
}
Serial.println();

sendCan(0x151, 0x10, firstFrameSize, payload[0], payload[1], payload[2], payload[3], payload[4], payload[5]);

  // Header
  payload[idx++] = 0x82;
  payload[idx++] = 0xFF;
  payload[idx++] = scrollLockIndicator;//0B to bottom 07 to up

  int maxHeaderLen = 26;
  int h = 0;
  while (header && header[h] && h < maxHeaderLen && idx < 96) {
    Serial.print("Header["); Serial.print(h); Serial.print("] = ");
    Serial.print(header[h]); Serial.print(" | idx = "); Serial.println(idx);
    payload[idx++] = header[h++];
  }

  Serial.print("[Header copy done] Final idx = "); Serial.println(idx);

  while (idx < 35) payload[idx++] = 0x00;

  // Item 1
  payload[idx++] = selectionItem1;
  payload[idx++] = 0x7E;
  for (int i = 0; i < 4; i++) {
    payload[idx++] = (i < strlen(item1)) ? item1[i] : ' ';
  }

  while (idx < 62) payload[idx++] = 0x00;

  // Item 2
  payload[idx++] = selectionItem2;
  payload[idx++] = 0x7F;
  while (*item2 && idx < 96) payload[idx++] = *item2++;
  while (idx < 96) payload[idx++] = 0x00;

  Serial.print("[PayloadLength] "); Serial.println(idx);

  uint8_t seq = 1;
  for (int i = 6; i < idx; i += 7) {
    uint8_t d[8];
    d[0] = 0x20 | (seq++ & 0x0F);
    for (int j = 0; j < 7; j++) {
      d[j + 1] = (i + j < idx) ? payload[i + j] : 0x00;
    }

    Serial.print("[CAN] Frame "); Serial.print(seq - 1); Serial.print(": ");
    for (int b = 0; b < 8; b++) {
      Serial.print(d[b], HEX); Serial.print(" ");
    }
    Serial.println();

    sendCan(0x151, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    delay(5);
  }

  Serial.println("[showMenu] --- Done ---");
}

void showInfoMenu(
  const char* item1 = "AUX   AUTO",
  const char* item2 = "AF ON",
  const char* item3 = "SPEED 0",
  uint8_t offset1 = 0x41,
  uint8_t offset2 = 0x44,
  uint8_t offset3 = 0x48, 
  uint8_t infoPrefix = 0x60
) {
  Serial.println("[showInfoMenu] --- Sending Info Menu ---");

  auto sendMenuItem = [&](uint8_t offset, const char* text, const char* label) {
    char padded[8] = { ' ' };
    strncpy(padded, text, 8); // Max 8 characters for display

    Serial.print("[MenuItem] "); Serial.print(label);
    Serial.print(" | Offset: 0x"); Serial.print(offset, HEX);
    Serial.print(" | Text: \""); Serial.print(padded); Serial.println("\"");

    // Frame 1
    sendCan(0x151, 0x10, 0x0B, 0x76, infoPrefix, offset, padded[0], padded[1], padded[2]);
    Serial.printf("  [CAN] %03X -> 10 0B %02X %02X %02X %02X %02X\n",
      0x151, 0x76, infoPrefix, offset, padded[0], padded[1], padded[2]);
    delay(5);

    // Frame 2
    sendCan(0x151, 0x21, padded[3], padded[4], padded[5], padded[6], padded[7], 0x00, 0x00);
    Serial.printf("  [CAN] %03X -> 21 %02X %02X %02X %02X %02X 00 00\n",
      0x151, padded[3], padded[4], padded[5], padded[6], padded[7]);
    delay(5);
  };

  sendMenuItem(offset1, item1, "Item1");
  sendMenuItem(offset2, item2, "Item2");
  sendMenuItem(offset3, item3, "Item3");

  Serial.println("[showInfoMenu] --- Done ---");
}


void printFrame_inline(CAN_FRAME &frame)
{
	// Print message
	Serial.print("ID: ");
	Serial.print(frame.id,HEX);
	Serial.print("Ext: ");
	if(frame.extended) {
		Serial.print("Y"); 
	} else {
		Serial.print("N");
	}
	Serial.print("Len: ");
	Serial.print(frame.length,DEC);
	Serial.print("{");

	for(int i = 0;i < frame.length; i++) {
		Serial.print(frame.data.uint8[i],HEX);
		Serial.print(" ");
	}
	Serial.println("}");
}

//  
SerialCommand sCmd; // The SerialCommand object
// put function declarations here:
// int myFunction(int, int);
void testShowInfoMenu() {
  char* offset1str = sCmd.next();
  char* offset2str = sCmd.next();
  char* offset3str = sCmd.next();
  char* infoPrefixstr = sCmd.next();

  char* item1 = sCmd.next();
  char* item2 = sCmd.next();
  char* item3 = sCmd.next();

  if (!offset1str || !offset2str || !offset3str || !infoPrefixstr) {
    Serial.println("[testShowInfoMenu] Error: Missing offset/prefix args.");
    Serial.println("Usage: menuit <offset1> <offset2> <offset3> <prefix> <item1> <item2> <item3>");
    return;
  }

  uint8_t offset1 = strtol(offset1str, nullptr, 16);  
  uint8_t offset2 = strtol(offset2str, nullptr, 16);  
  uint8_t offset3 = strtol(offset3str, nullptr, 16);  
  uint8_t infoPrefix = strtol(infoPrefixstr, nullptr, 16);  

  // fallback text if missing
  const char* def1 = "AUX AUTO";
  const char* def2 = "AF ON";
  const char* def3 = "SPEED 0";

  showInfoMenu(
    item1 ? item1 : def1,
    item2 ? item2 : def2,
    item3 ? item3 : def3,
    offset1, offset2, offset3, infoPrefix
  );
}
// This gets set as the default handler, and gets called when no other command matches.
void unrecognized(const char *command)
{
  Serial.println(F("What?"));
  Serial.print(F("Unrecognized command: "));
  Serial.println(command); // Write the unrecognized command to Serial
  Serial.println("Ready ...!");  
}


void emulateKey(uint16_t key, bool hold = false ) {
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
    frame.data.uint8[3] |= AFFA2_KEY_HOLD_MASK;
  }
  
  CAN0.sendFrame(frame);

  Serial.print("Emulated key press: 0x");
  Serial.println(key, HEX);
}

void sendPasswordSequence() {
  // 5
  for (int i = 0; i < 5; i++) {
    emulateKey(AFFA2_KEY_ROLL_UP);
    delay(100);
  }
  emulateKey(AFFA2_KEY_LOAD);
  delay(200);

  // 3
  for (int i = 0; i < 3; i++) {
    emulateKey(AFFA2_KEY_ROLL_UP);
    delay(100);
  }
  emulateKey(AFFA2_KEY_LOAD);
  delay(200);

  // 2
  for (int i = 0; i < 2; i++) {
    emulateKey(AFFA2_KEY_ROLL_UP);
    delay(100);
  }
  emulateKey(AFFA2_KEY_LOAD);
  delay(200);

  // 1 
  for (int i = 0; i < 1; i++) {
    emulateKey(AFFA2_KEY_ROLL_UP);
    delay(100);
  }

  emulateKey(AFFA2_KEY_LOAD, true);  // <-- hold
}

void onPressCommand(){
  char *arg = sCmd.next();  
  if (!arg) {
    Serial.println("No key specified");
    return;
  }

  if (strcmp(arg, "pause") == 0) emulateKey(AFFA2_KEY_PAUSE);
  else if (strcmp(arg, "next") == 0) emulateKey(AFFA2_KEY_ROLL_UP);
  else if (strcmp(arg, "prev") == 0) emulateKey(AFFA2_KEY_ROLL_DOWN);
  else if (strcmp(arg, "volup") == 0) emulateKey(AFFA2_KEY_VOLUME_UP);
  else if (strcmp(arg, "voldown") == 0) emulateKey(AFFA2_KEY_VOLUME_DOWN);
  else if (strcmp(arg, "load") == 0) emulateKey(AFFA2_KEY_LOAD);
  else if (strcmp(arg, "src_left") == 0) emulateKey(AFFA2_KEY_SRC_LEFT);
  else if (strcmp(arg, "src_right") == 0) emulateKey(AFFA2_KEY_SRC_RIGHT);
  else if (strcmp(arg, "load_hold") == 0) emulateKey(AFFA2_KEY_LOAD,true);
  else if (strcmp(arg, "pass") == 0) sendPasswordSequence(); 
 
  else Serial.println("Unknown key name");
}



#pragma endregion

void printFrame(CAN_FRAME *frame, int mailbox = -1){
  Serial.print("got message in mailbox []");
  Serial.println(mailbox);
  
  Serial.print("CAN MSG: 0x");
  Serial.print(frame->id, HEX);
  Serial.print(" [");
  Serial.print(frame->length, DEC);
  Serial.print("] <");
  for (int i = 0; i < frame->length; i++)
  {
    if (i != 0)
      Serial.print(":");
    Serial.print(frame->data.byte[i], HEX);
  }
  Serial.println(">");
}


#define AFFA3_KEY_LOAD               0x0000 /* Ten na dole pilota ;) */
#define AFFA3_KEY_SRC_RIGHT          0x0001
#define AFFA3_KEY_SRC_LEFT           0x0002
#define AFFA3_KEY_VOLUME_UP          0x0003
#define AFFA3_KEY_VOLUME_DOWN        0x0004
#define AFFA3_KEY_PAUSE              0x0005
#define AFFA3_KEY_ROLL_UP            0x0101
#define AFFA3_KEY_ROLL_DOWN          0x0141

#define AFFA3_KEY_HOLD_MASK          (0x80 | 0x40)

// Callback function for frame with ID 0x1C1
void gotFrame_0x1C1(CAN_FRAME *packet) 
{
  printFrame(packet, 0);
  if (1==1/*without radio*/);
  {
    sendCan(0x5C1,0x74,0,0,0,0,0,0,0);
  }
  
    if(current_in_AUX_mode ){

      if ((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] != 0x89)) /* Błędny pakiet */
      {
        Serial.println("bledny packet");
        return;
      }
      
      
      // Extract key value
      uint16_t key = (packet->data.uint8[2] << 8) | packet->data.uint8[3];
      
      // Handle key actions
      switch (key) 
    {
      case AFFA3_KEY_PAUSE:
      Serial.println("Pause/Play");
      bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
      break;
      
      case AFFA3_KEY_ROLL_UP: // Next track
      Serial.println("Next Track");
      bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
      break;
      
      case AFFA3_KEY_ROLL_DOWN: // Previous track
      Serial.println("Previous Track");
      bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
      break;
      
      case AFFA3_KEY_LOAD: // load
      Serial.println("loading some text"); 
      break;
      
      default:
      Serial.print("Unknown key: 0x");
      Serial.println(key, HEX);
      break;
    }
    
  }
} 
bool sessionStarted = false;
unsigned long lastPingTime = 0;

void display_Control( int8_t state){

  if(state == 1 )
    sendCan(151,3,	52,		9,0,	0	,0,	0,	0);//sc 151 3 52 9 0 0 0 0 0
  
  else   
    sendCan(151,3,	52,		0,0,	0	,0,	0,	0);//sc 151 3 52 9 0 0 0 0 0
  
}


  // Callback function for frame with ID 0x151
  void gotFrame_0x3AF(CAN_FRAME *packet) {}

// You can also declare your ping timeout threshold if needed
  #define AFFA3_PING_TIMEOUT 5000 // 5 seconds, or adjust as needed

  // Callback function for frame with ID 0x151
  void gotFrame_0x3CF(CAN_FRAME *packet) 
  {
    //printFrame(packet,-2);
    struct CAN_FRAME answer;
    
	  static int8_t timeout = AFFA3_PING_TIMEOUT;
    // IF("AFFA3"){

    if (packet->data.uint8[0] == 0x61 && packet->data.uint8[1] == 0x11) {
      // Auth request
      sendCan(0x3AF, 0xBA, 0, 0, 0, 0, 0, 0, 0);
      delay(50);
      sendCan(0x3AF, 0xB0, 0x14, 0x11, 0x00, 0x1F, 0x00, 0x00, 0x00);
      sendCan(0x3AF, 0xB0, 0x14, 0x11, 0x00, 0x1F, 0x00, 0x00, 0x00);
      sessionStarted = true;
      delay(50);
      sendCan(0x151, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00); //display registration
      sendCan(0x1F1, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00); //display registration
    }

    if (packet->data.uint8[0] == 0x69 && (packet->data.uint8[1] == 0x00 || packet->data.uint8[1] == 0x01)) {
      // Ping
      sendCan(0x3AF, 0xB9, 0, 0, 0, 0, 0, 0, 0);
    }
    // Periodic keep-alive
    if (sessionStarted && millis() - lastPingTime > 5000) {
      sendCan(0x3AF, 0xB9, 0, 0, 0, 0, 0, 0, 0);
 
      lastPingTime = millis();
    }
  }

  AuxModeTracker tracker;

  // Callback function for frame with ID 0x151
  void gotFrame_0x151(CAN_FRAME *packet) 
  {
    // if(/*withouit radio*/1==1){
    //   sendCan(0x551,0x74,)
    // }
    tracker.onCanMessage(*packet);
  
    // Extract key text from CAN message
    if (packet->data.uint8[0] == 0x21) // Start of the text payload
    {
      if (packet->data.uint8[1] == 0x20
      &&packet->data.uint8[2] == 0x20
      &&packet->data.uint8[3] == 0xB0
      &&packet->data.uint8[4] == 0x30
      &&packet->data.uint8[5] == 0x30
      &&packet->data.uint8[6] == 0x30
      &&packet->data.uint8[7] == 0x20
      )
      {
              //input password
              delay(1000);
              sendPasswordSequence(); 
      } 
    }
}

// General callback function for frames that don't match the specific IDs
void gotFrame(CAN_FRAME *frame) 
{
  //printFrame(frame, -1);
}
void setTime() {
  char* yearStr = sCmd.next();
  if (!yearStr || strlen(yearStr) != 4) {
    Serial.println("Usage: setTime <YYYY>");
    return;
  }

  CAN_FRAME answer;
  answer.id = 0x151;
  answer.length = 8;
  answer.data.uint8[0] = 0x05;
  answer.data.uint8[1] = 'V';              // likely constant
  answer.data.uint8[2] = yearStr[0];
  answer.data.uint8[3] = yearStr[1];
  answer.data.uint8[4] = yearStr[2];
  answer.data.uint8[5] = yearStr[3];
  answer.data.uint8[6] = 0x00;
  answer.data.uint8[7] = 0x00;

  CAN0.sendFrame(answer);
  Serial.print("Sent time set frame with year: ");
  Serial.println(yearStr);
}
#define DISPLAY_CAN_ID 0x151
#define DISPLAY_TEXT_LENGTH 14
#define CAN_DELAY_MS 5

void sendDisplayFrame(uint8_t frameIndex, const char* textSegment) {
  CAN_FRAME frame;
  frame.id = DISPLAY_CAN_ID;
  frame.length = 8;
  frame.data.uint8[0] = 0x21 + frameIndex; // 0x21, 0x22, ...
  
  for (int i = 0; i < 7; i++) {
    frame.data.uint8[i + 1] = textSegment[i];
  }

  CAN0.sendFrame(frame);
  Serial.println("Header sended:" );
  printFrame_inline(frame); 
  delay(CAN_DELAY_MS);
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
  frame.id = DISPLAY_CAN_ID;
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
  printFrame_inline(frame);
  delay(CAN_DELAY_MS);
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
  int speed = 300,
  int count = 1,
  const char* padding = "        "  // 8 spaces
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
void testShowMenuLimits() {
  Serial.println("=== Testing Menu Limits ===");

  const char* testHeaders[] = {
    "Short",
    "12345678901234567890",         // 20 chars
    "123456789012345678901",        // 21
    "1234567890123456789012",       // 22
    "123456789012345678901234",     // 24
    "12345678901234567890123456",   // 26
    "1234567890123456789012345678", // 28
    nullptr
  };

  const char* testItems[] = {
    "One",
    "1234567890",
    "ABCDEFGHIJKL",
    "This is a really long item string for testing truncation",
    nullptr
  };

  for (int h = 0; testHeaders[h] != nullptr; h++) {
    for (int i1 = 0; testItems[i1] != nullptr; i1++) {
      for (int i2 = 0; testItems[i2] != nullptr; i2++) {
        Serial.println("-----");
        Serial.print("Header: "); Serial.println(testHeaders[h]);
        Serial.print("Item1: ");  Serial.println(testItems[i1]);
        Serial.print("Item2: ");  Serial.println(testItems[i2]);
        showMenu(testHeaders[h], testItems[i1], testItems[i2]);
        delay(500);
      }
    }
  }

  Serial.println("=== Test Complete ===");
}

void handleMenuCmd(){
 // char* selectedCmd = sCmd.next(); 
  
  char* header      = sCmd.next();  // optional
  char* item1      = sCmd.next();  // optional
  char* item2    = sCmd.next();  // optional

  // int selected = 0;
  // if (selectedCmd) selected = atoi(selectedCmd);
  showMenu(header,item1,item2);

}

void handleTextCmd(){
  char* textStr       = sCmd.next();  // the actual text
  
  uint8_t mode       = 0x77; // strtol(modeStr,       nullptr, 16);
  uint8_t rdsIcon    = 0x55; //strtol(rdsIconStr,    nullptr, 16);
  uint8_t sourceIcon = 0xFF; // strtol(sourceIconStr, nullptr, 16);
  uint8_t textFormat = 0x60; // strtol(textFormatStr, nullptr, 16);

  sendTextToDisplay(mode, rdsIcon, sourceIcon, textFormat, textStr);
}
void handleScrollCmd() {

  char* textStr       = sCmd.next();  // the actual text
  char* speedStr      = sCmd.next();  // optional
  char* countStr      = sCmd.next();  // optional
  char* paddingStr    = sCmd.next();  // optional

  if ( !textStr) {
    Serial.println(F("Usage: scroll <text> [speed] [count] [padding]"));
    return;
  }

  uint8_t mode       = 0x77; // strtol(modeStr,       nullptr, 16);
  uint8_t rdsIcon    = 0x55; //strtol(rdsIconStr,    nullptr, 16);
  uint8_t sourceIcon = 0xFF; // strtol(sourceIconStr, nullptr, 16);
  uint8_t textFormat = 0x60; // strtol(textFormatStr, nullptr, 16);

  int speed = 300;
  int count = 1;
  if (speedStr) speed = atoi(speedStr);
  if (countStr) count = atoi(countStr);

  const char* padding = "       ";
  if (paddingStr) padding = paddingStr;

  scrollTextRightToLeft(mode, rdsIcon, sourceIcon, textFormat, textStr, speed, count, padding);
}

void sendText_internal(uint8_t byte3, uint8_t byte4, uint8_t byte5, uint8_t byte6, uint8_t byte7, uint8_t byte8, const char* text) {
  // Ensure text is 14 characters long (pad with spaces if necessary)
 
  

  String textStr = String(text);
  textStr.replace("_", " "); // use underscores for spaces
  while (textStr.length() < 32) textStr += ' ';

  CAN_FRAME answer;

  // Frame 1
  answer.id = 0x151;
  answer.length = AFFA3_PACKET_LEN; 

  //10 69 21 05 FF 00 00 49  -long text setted
  answer.data.uint8[0] = 0x10;
  answer.data.uint8[1] = 0x0E;
  answer.data.uint8[2] = byte3;// 74- full window, 77-not full. if sended not full when not applid - it fill freze at main screen.              
   //sc 151 2 54 3 0 0 0 0 0  to close full window
  answer.data.uint8[3] = byte4;//45= AF-RDS 55-none
  answer.data.uint8[4] = byte5; //???55 ALSWAYS
  answer.data.uint8[5] = byte6;// ADD df-MANU / FF - NOTHING  FD -PRESET ??- LIST
  answer.data.uint8[6] = byte7;//19-3f 5sym + point+ 2sym + channel.  (19 for ✔️),59-7F plain 8 sym + Channel ascii
  answer.data.uint8[7] = byte8;//always 1( when 0 showing scoll menu???)
  //sc 151  04 25 00 03 00 00 00 00  - shows "CD" ico (BOOL? MOD/2?)
  //sc 151  04 25 00 00 00 00 00 00  - shows "CD" ico nothing ico
  CAN0.sendFrame(answer);
  delay(5);

  // Frame 2
  answer.data.uint8[0] = 0x21;
  for (int i = 0; i < 7; i++) {
    answer.data.uint8[i + 1] = textStr[i];
  }

  CAN0.sendFrame(answer);
  delay(5);

  // Frame 3
  answer.data.uint8[0] = 0x22;
  for (int i = 0; i < 7; i++) {
    answer.data.uint8[i + 1] = textStr[i + 7];
  }

  

  CAN0.sendFrame(answer);
  Serial.println("Sent all 3 CAN frames:");
  printFrame(&answer);
}


void showConfirmBoxWithOffsets(
  const char* caption,
  const char* row1,
  const char* row2,
  const char* row3 
) {
  Serial.println("[showConfirmBoxWithOffsets] --- Sending Custom Confirm Box ---");

  // ISO-TP total payload: 112 bytes (16 frames × 7 bytes)
  uint8_t payload[112] = {0};  // Initialize all bytes to 0
  uint8_t currentFillEnd = 0;  // Tracks where the last write ended

  // Insert button caption at offset 0x1A (max 7 characters)
  for (uint8_t i = 0; i < 7 && caption[i]; i++) {
    payload[0x1A + i] = caption[i];
  }


  // Insert rows with 0x20 between them, starting at 0x20
  uint8_t offset = 0x20;

  auto insertRow = [&](const char* text) {
    while (*text && offset < 0x36) {
      payload[offset++] = *text++;
    }
    // Add 0x20 to separate rows (unless last one)
    if (offset < 0x36) {
      payload[offset++] = 0xD;
    }
  };
  insertRow(row1); 
  insertRow(row2); 
  insertRow(row3);

 

  // Now send CAN frames
  // First frame (0x10): initialize ISO-TP with first data byte (0x6F)
  sendCan(0x151, 0x10, 0x6F, 0x21, 0x05, 0x00, 0x00, 0x01, 0x49);

  uint8_t payloadIndex =0;

  // Now send 15 more frames: 0x21 to 0x2F
  for (uint8_t i = 0; i < 15; i++) {
    uint8_t pci = 0x21 + i;  // Frame identifier
    uint8_t data[8] = {0};    // Data array to store 8 bytes

    data[0] = pci;  // First byte is the frame identifier
    // Fill the remaining 7 bytes with payload data, from the correct offset
    for (uint8_t j = 0; j < 7; j++) {
      
      data[j + 1] = payload[payloadIndex++]; 
    }

    // Send the CAN frame with the correct data
    sendCan(0x151, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

    // Debugging: Print the CAN message sent
    Serial.printf("  [CAN] %03X -> %02X %02X %02X %02X %02X %02X %02X %02X\n",
      0x151, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

    delay(5);  // Small delay between frames
  }

  Serial.println("[showConfirmBoxWithOffsets] --- Done ---");
}

void testConfirmBoxCmd() {
  const char* l1 = sCmd.next();
  const char* l2 = sCmd.next();
  const char* footer = sCmd.next();
  const char* l4 = sCmd.next();

  if (!l1) l1 = "Occupied: 3";
  if (!l2) l2 = "Avail.: 47";
  if (!footer) footer = "Confirm?";
 

  showConfirmBoxWithOffsets(l1,l2,footer,l4);
}



void sendCAN()
{
//sendCAN 77 55 55 FD 60 1 BULLDOZZER 
    char* b3Str = sCmd.next();
    char* b4Str = sCmd.next();
    char* b5Str = sCmd.next();
    char* b6Str = sCmd.next();
    char* b7Str = sCmd.next();
    char* b8Str = sCmd.next();
    char* text  = sCmd.next();  // Keep appending the text
    
    //Serial.print("Full Text: ");
    //Serial.println(text); 
    
    
    if (!b6Str || !b7Str || !text) {
      Serial.println("Usage: sendCAN <byte6> <byte7> <text>");
      return;
    }
    Serial.println("Got mesage:");  // Print current segment for debugging
    Serial.println(text);  // Print current segment for debugging


    uint8_t byte3 = strtol(b3Str, nullptr, 16); //display mode: 77 for  full screen, 74 - in window  (default 77)
    uint8_t byte4 = strtol(b4Str, nullptr, 16); //rds ico: 45= AF-RDS 55-none  (default 55)
    uint8_t byte5 = strtol(b5Str, nullptr, 16); //always 55,idk what it mean
    uint8_t byte6 = strtol(b6Str, nullptr, 16); //shows type ico: df-MANU / FF - NOTHING / FD -PRESET /??- LIST
    uint8_t byte7 = strtol(b7Str, nullptr, 16); //19-3f 5symbols + point + 2sym + channel.  (19 for ✔️),59-7F plain 8 sym + Channel ascii
    //so if you send 19-3f it shows text with period dot at the end, starting from second symbol. for example sometext=> will apear like omete.x 
    //if you send 59-7f it shows text with period dot at the end, starting from second symbol. for example sometext=> will apear like omete.x  
    // its also shows somechannel symbol based on asccii code, for example to show 9 you need send 39 or 79, for show # - 23 or 63 
    uint8_t byte8 = strtol(b8Str, nullptr, 16); //always 1



  // Ensure the text is at least 14 characters long (pad with spaces if necessary)
  String textStr = String(text);
  while (textStr.length() < 14) {
    textStr += ' ';
  } 
    // Send the text in parts (8-character chunks)
    sendText_internal(byte3, byte4, byte5, byte6, byte7, byte8, textStr.c_str()); 

}

void handleSerialMenuCommand() {
 

  // Parse parameters
  const char* header = "Dest. memory";// sCmd.next();
  const char* item1 = "FFFF";// sCmd.next();
  const char* item2 = "GROSS-ZIMMERN";// sCmd.next();

  uint8_t frameSize     =  0x5A;
  uint8_t scrollLock    =  strtoul(sCmd.next(), NULL, 16); //: 0x0B - bottom;//0B to bottom 07 to up
  uint8_t selItem1      =  strtoul(sCmd.next(), NULL, 16); //: 0x00;
  uint8_t selItem2      =  strtoul(sCmd.next(), NULL, 16); //: 0x01;

  Serial.println("[SerialCmd] Parsed Menu Command:");
  Serial.println("Header: " + String(header));
  Serial.println("Item1: " + String(item1));
  Serial.println("Item2: " + String(item2));
  Serial.print("FrameSize: 0x"); Serial.println(frameSize, HEX);
  Serial.print("ScrollLock: 0x"); Serial.println(scrollLock, HEX);
  Serial.print("SelItem1: 0x"); Serial.println(selItem1, HEX);
  Serial.print("SelItem2: 0x"); Serial.println(selItem2, HEX);
  //so i have look at log and i think that we need just send new sygnal 07 29 01 7E 80 00 00 00  or 07 29 01 7F 80 00 00 00  to set selected item

  showMenu(header, item1, item2, frameSize, scrollLock, selItem1, selItem2);
}

void handleSelectMenuItem() {
  const char* indexStr = sCmd.next(); // e.g., "01"
  const char* flagStr = sCmd.next();  // e.g., "7E" or "7F"

  if (!indexStr || !flagStr) {
    Serial.println("Usage: selectItem <index_hex> <flag_hex>");
    return;
  }

  uint8_t index = strtol(indexStr, NULL, 16);
  uint8_t flag = strtol(flagStr, NULL, 16);

  // ID 0x151, data: 07 29 <index> <flag> 80 00 00 00
  sendCan(0x151, 0x07, 0x29, index, flag, 0x80, 0x00, 0x00, 0x00);
  Serial.print("Sent selection update: index=0x");
  Serial.print(index, HEX);
  Serial.print(" flag=0x");
  Serial.println(flag, HEX);
}

void sendGenericCAN() {
  char* idStr = sCmd.next();
  if (!idStr) {
    Serial.println("Usage: sendCAN <hex_id> <8 bytes in hex>");
    return;
  }

  uint32_t msgID = (uint32_t) strtol(idStr, NULL, 16);

  CAN_FRAME frame;
  frame.id = msgID;
  frame.length = 8;
  for (int i = 0; i < 8; i++) {
    char* byteStr = sCmd.next();
    if (!byteStr) {
      Serial.println("Not enough bytes. Provide 8.");
      return;
    }
    frame.data.uint8[i] = (uint8_t) strtol(byteStr, NULL, 16);
  }

  CAN0.sendFrame(frame);
  Serial.print("Sent CAN ID 0x");
  Serial.print(msgID, HEX);
  Serial.print(" with data: ");
  for (int i = 0; i < 8; i++) {
    Serial.printf("%02X ", frame.data.uint8[i]);
  }
  Serial.println();
}



void setup()
{

  
  Serial.begin(115200);
  bleKeyboard.begin();

  Serial.println("------------------------");
  Serial.println("    MrDIY CAN SHIELD v0.1");
  Serial.println("------------------------");

  Serial.println(" CAN...............INIT");
  CAN0.setCANPins(GPIO_NUM_4, GPIO_NUM_5); // config for shield v1.3+, see important note above!
  CAN0.begin(CAN_BPS_500K);                      // 500Kbps


  // Set filters to only catch frames with ID 0x1C1 and 0x151
    CAN0.setRXFilter(0, 0x1C1, 0x7FF, false);  // Catch frames with ID 0x1C1
    CAN0.setRXFilter(1, 0x151, 0x7FF, false);  // Catch frames with ID 0x151
    if(1==1/*syncronization????*/){
      CAN0.setRXFilter(2, 0x3CF, 0x7FF, false);  // Catch frames with ID 0x3cF 
      CAN0.setCallback(2, gotFrame_0x3CF);  // Frame with ID 0x151 will trigger this callback
    }
    CAN0.setRXFilter(3, 0x3AF, 0x7FF, false);  // Catch frames with ID 0x1C1
  
  // // Register callback functions for specific IDs
  CAN0.setCallback(0, gotFrame_0x1C1);  // Frame with ID 0x1C1 will trigger this callback
  CAN0.setCallback(1, gotFrame_0x151);  // Frame with ID 0x151 will trigger this callback
  CAN0.setCallback(3, gotFrame_0x3AF);  // Frame with ID 0x151 will trigger this callback
  
  // // General callback function for any other frame that doesn't match the above filters
  CAN0.watchFor();
  CAN0.setGeneralCallback(gotFrame);
 




  Serial.println(" CAN............500Kbps");
   
  // // Setup callbacks for SerialCommand commands
 // sCmd.addCommand("t", testDisp);         // print test message on display
  sCmd.addCommand("p", onPressCommand);
  sCmd.addCommand("sendCAN", sendCAN);
  sCmd.addCommand("sc", sendGenericCAN);
  sCmd.addCommand("scroll", handleScrollCmd);
  sCmd.addCommand("text", handleTextCmd);
  sCmd.addCommand("menu", handleMenuCmd);
  sCmd.addCommand("menuit", testShowInfoMenu);
  sCmd.addCommand("menut", testShowMenuLimits);
  sCmd.addCommand("menut2", handleSerialMenuCommand);
  sCmd.addCommand("selectItem", handleSelectMenuItem);
  sCmd.addCommand("cb", testConfirmBoxCmd);
  sCmd.addCommand("setTime", setTime);
  // sCmd.addCommand("ss", startSync);       // 
  // sCmd.addCommand("sd", syncDisp);        //
  // sCmd.addCommand("r", registerDisplay);  //
  // sCmd.addCommand("e", enableDisplay);    //
  // sCmd.addCommand("i", initDisplay);      //
  // sCmd.addCommand("mt", messageTest);     //
  // sCmd.addCommand("m", messageTextIcons); //
  // sCmd.addCommand("d", displaySong);      //
  sCmd.setDefaultHandler(unrecognized);   // Handler for command that isn't matched  (says "What?")
 
 
} 
 
 
#define SYNC_INTERVAL_MS 500
static uint32_t last_sync = 0;
 
void loop()
{ 
  sCmd.readSerial(); 
 
    uint32_t now = millis(); // Or your system time function

    if (now - last_sync > SYNC_INTERVAL_MS) { 
        //affa3_tick2();
        last_sync = now;
    }

    // Add more periodic logic if needed
} 
 

