#include <Arduino.h>

#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
#include "AuxModeTracker.h"
  

#include <SerialCommand.h> // Assuming this is already included in your project

 
#include <BleKeyboard.h>
#include "Affa3Display.h"
// Replace with your network credentials 


AuxModeTracker tracker;
// Web server running in AP mode 


// Create an AsyncWebServer object on port 80
//AsyncWebServer server(80);


//for menu navigation
// sc 151 7 29 1 7F 80 0 0 0
// sc 151 7 29 1 7E 80 0 0 0



#define AFFA3_PACKET_LEN 0x08
#define AFFA3_KEY_LOAD 0x0000 /* This at the bottom of the remote;) */
#define AFFA3_KEY_SRC_RIGHT 0x0001
#define AFFA3_KEY_SRC_LEFT 0x0002
#define AFFA3_KEY_VOLUME_UP 0x0003
#define AFFA3_KEY_VOLUME_DOWN 0x0004
#define AFFA3_KEY_PAUSE 0x0005
#define AFFA3_KEY_ROLL_UP 0x0101
#define AFFA3_KEY_ROLL_DOWN 0x0141
#define AFFA3_KEY_HOLD_MASK (0x80 | 0x40)







 
BleKeyboard bleKeyboard("MeganeCAN","gycer",100);

 
void unrecognized(const char *command); 
 

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

  Affa3Display::showInfoMenu(
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



void sendPasswordSequence() {
  // 5
  for (int i = 0; i < 5; i++) {
    Affa3Display::emulateKey(AFFA3_KEY_ROLL_UP);
    delay(100);
  }
  Affa3Display::emulateKey(AFFA3_KEY_LOAD);
  delay(200);

  // 3
  for (int i = 0; i < 3; i++) {
    Affa3Display::emulateKey(AFFA3_KEY_ROLL_UP);
    delay(100);
  }
  Affa3Display::emulateKey(AFFA3_KEY_LOAD);
  delay(200);

  // 2
  for (int i = 0; i < 2; i++) {
    Affa3Display:: emulateKey(AFFA3_KEY_ROLL_UP);
    delay(100);
  }
  Affa3Display::emulateKey(AFFA3_KEY_LOAD);
  delay(200);

  // 1 
  for (int i = 0; i < 1; i++) {
    Affa3Display:: emulateKey(AFFA3_KEY_ROLL_UP);
    delay(100);
  }

  Affa3Display::emulateKey(AFFA3_KEY_LOAD, true);  // <-- hold
}

void onPressCommand(){
  char *arg = sCmd.next();  
  if (!arg) {
    Serial.println("No key specified");
    return;
  }

  if (strcmp(arg, "pause") == 0) Affa3Display::emulateKey(AFFA3_KEY_PAUSE);
  else if (strcmp(arg, "next") == 0) Affa3Display::emulateKey(AFFA3_KEY_ROLL_UP);
  else if (strcmp(arg, "prev") == 0)Affa3Display:: emulateKey(AFFA3_KEY_ROLL_DOWN);
  else if (strcmp(arg, "volup") == 0)Affa3Display:: emulateKey(AFFA3_KEY_VOLUME_UP);
  else if (strcmp(arg, "voldown") == 0) Affa3Display::emulateKey(AFFA3_KEY_VOLUME_DOWN);
  else if (strcmp(arg, "load") == 0) Affa3Display::emulateKey(AFFA3_KEY_LOAD);
  else if (strcmp(arg, "src_left") == 0)Affa3Display:: emulateKey(AFFA3_KEY_SRC_LEFT);
  else if (strcmp(arg, "src_right") == 0) Affa3Display::emulateKey(AFFA3_KEY_SRC_RIGHT);
  else if (strcmp(arg, "load_hold") == 0) Affa3Display::emulateKey(AFFA3_KEY_LOAD,true);
  else if (strcmp(arg, "pass") == 0) sendPasswordSequence(); 
 
  else Serial.println("Unknown key name");
}



#pragma endregion
 

float getVoltage() {
  return 13.5; // Stub for now
}

void ShowMyInfoMenu(){
  float voltage = getVoltage();
  char voltageStr[16];
  snprintf(voltageStr, sizeof(voltageStr), "Voltage: %.1fV", voltage);
  Affa3Display::showMenu("MeganeCAN", voltageStr, "Color: ORANGE");
}
// Callback function for frame with ID 0x1C1
void gotFrame_0x1C1(CAN_FRAME *packet) 
{
  printFrame_inline(*packet);
  if (1==1/*without radio*/);
  {
    Affa3Display::sendCan(0x5C1,0x74,0,0,0,0,0,0,0);
  }
  
    if( tracker.isInAuxMode() ){

      if ((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] != 0x89)) /* Błędny pakiet */
      {
        Serial.println("bledny packet");
        return;
      }
      
       
      // Extract full key
      uint16_t rawKey = (packet->data.uint8[2] << 8) | packet->data.uint8[3];

      // Mask out hold bits for key comparison
      uint16_t key = rawKey & ~AFFA3_KEY_HOLD_MASK;

      // Detect hold status
      bool isHold = (rawKey & AFFA3_KEY_HOLD_MASK) != 0;

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
        if (isHold)
        {
          ShowMyInfoMenu();
        }
      
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
 

  // Callback function for frame with ID 0x151
  void gotFrame_0x3AF(CAN_FRAME *packet) {}

// You can also declare your ping timeout threshold if needed
  static int8_t timeout = AFFA3_PING_TIMEOUT;

  // Callback function for frame with ID 0x151
  void gotFrame_0x3CF(CAN_FRAME *packet) 
  {
    //printFrame(packet,-2);
    struct CAN_FRAME answer;
    
	  static int8_t timeout = AFFA3_PING_TIMEOUT;
    // IF("AFFA3"){

    if (packet->data.uint8[0] == 0x61 && packet->data.uint8[1] == 0x11) {
      // Auth request
      Affa3Display::sendCan(0x3AF, 0xBA, 0, 0, 0, 0, 0, 0, 0);
      delay(50);
      Affa3Display::sendCan(0x3AF, 0xB0, 0x14, 0x11, 0x00, 0x1F, 0x00, 0x00, 0x00);
      Affa3Display::sendCan(0x3AF, 0xB0, 0x14, 0x11, 0x00, 0x1F, 0x00, 0x00, 0x00);
      sessionStarted = true;
      delay(50);
      Affa3Display::sendCan(0x151, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00); //display registration
      Affa3Display::sendCan(0x1F1, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00); //display registration
    }

    if (packet->data.uint8[0] == 0x69 && (packet->data.uint8[1] == 0x00 || packet->data.uint8[1] == 0x01)) {
      // Ping
      Affa3Display:: sendCan(0x3AF, 0xB9, 0, 0, 0, 0, 0, 0, 0);
    }
    // Periodic keep-alive
    if (sessionStarted && millis() - lastPingTime > 5000) {
      Affa3Display:: sendCan(0x3AF, 0xB9, 0, 0, 0, 0, 0, 0, 0);
 
      lastPingTime = millis();
    }
  }
 

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
  Affa3Display::setTime(yearStr);
}

#define DISPLAY_CAN_ID 0x151
#define DISPLAY_TEXT_LENGTH 14
#define CAN_DELAY_MS 5



void handleMenuCmd(){
 // char* selectedCmd = sCmd.next(); 
  
  char* header      = sCmd.next();  // optional
  char* item1      = sCmd.next();  // optional
  char* item2    = sCmd.next();  // optional

  // int selected = 0;
  // if (selectedCmd) selected = atoi(selectedCmd);
  Affa3Display::showMenu(header,item1,item2);

}

void handleTextCmd(){
  char* textStr       = sCmd.next();  // the actual text
  
  uint8_t mode       = 0x77; // strtol(modeStr,       nullptr, 16);
  uint8_t rdsIcon    = 0x55; //strtol(rdsIconStr,    nullptr, 16);
  uint8_t sourceIcon = 0xFF; // strtol(sourceIconStr, nullptr, 16);
  uint8_t textFormat = 0x60; // strtol(textFormatStr, nullptr, 16);

  Affa3Display:: sendTextToDisplay(mode, rdsIcon, sourceIcon, textFormat, textStr);
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

  Affa3Display::scrollTextRightToLeft(mode, rdsIcon, sourceIcon, textFormat, textStr, speed, count, padding);
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
  printFrame_inline(answer);
}


void testConfirmBoxCmd() {
  const char* l1 = sCmd.next();
  const char* l2 = sCmd.next();
  const char* footer = sCmd.next();

  if (!l1) l1 = "Occupied: 3";
  if (!l2) l2 = "Avail.: 47";
  if (!footer) footer = "Confirm?";
 

 Affa3Display::showConfirmBoxWithOffsets(l1,l2,footer);
}



void sendCAN()
{
  //Custom method, with my diffrent annotation,didt throw away
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

  Affa3Display::showMenu(header, item1, item2, frameSize, scrollLock, selItem1, selItem2);
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
  Affa3Display::sendCan(0x151, 0x07, 0x29, index, flag, 0x80, 0x00, 0x00, 0x00);
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
  //sCmd.addCommand("menut", testShowMenuLimits);
  sCmd.addCommand("menut2", handleSerialMenuCommand);
  sCmd.addCommand("selectItem", handleSelectMenuItem);
  sCmd.addCommand("cb", testConfirmBoxCmd);
  sCmd.addCommand("setTime", setTime);
  sCmd.addCommand("my", ShowMyInfoMenu);
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
 

