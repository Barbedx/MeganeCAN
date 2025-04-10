#include <Arduino.h>

#include <esp32_can.h> /* https://github.com/collin80/esp32_can */

#include <SerialCommand.h>
#include <affa3.h>

#include <BleKeyboard.h>

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

static uint8_t _sync_status  = AFFA3_SYNC_STAT_FAILED; /* Status synchronizacji z wświetlaczem */
void printFrame2(CAN_FRAME &frame)
{
	// Print message
	Serial.print("ID: ");
	Serial.println(frame.id,HEX);
	Serial.print("Ext: ");
	if(frame.extended) {
		Serial.println("Y");
	} else {
		Serial.println("N");
	}
	Serial.print("Len: ");
	Serial.println(frame.length,DEC);
	for(int i = 0;i < frame.length; i++) {
		Serial.print(frame.data.uint8[i],HEX);
		Serial.print(" ");
	}
	Serial.println();
}

void show_text(char text[14]) {
  struct CAN_FRAME packets[3];
  uint8_t i;
  
  /* Budujemy pakiety */
  packets[0].id = 0x151;
  packets[0].rtr = 0;
  packets[0].length = 8;
  packets[0].data.uint8[0] = 0x10; //text start frame
  packets[0].data.uint8[1] = 0x0E; // lenght
  packets[0].data.uint8[2] = 0x77; //cmd1
  packets[0].data.uint8[3] = 0x60; // 54/14/55 ????
  packets[0].data.uint8[4] = 0x55; //always
  packets[0].data.uint8[5] = 0xFF; // alwaystext[0];
  packets[0].data.uint8[6] = 0x60; //60/71/73 ?????
  packets[0].data.uint8[7] = 0x1;//always
  
  packets[1].id = 0x121;
  packets[1].rtr = 0;
  packets[1].length = 8;
  packets[1].data.uint8[0] = 0x21;
  packets[1].data.uint8[1] = text[0];
  packets[1].data.uint8[2] = text[1];
  packets[1].data.uint8[3] = text[2];
  packets[1].data.uint8[4] = text[3];
  packets[1].data.uint8[5] = text[4];
  packets[1].data.uint8[6] = text[5];
  packets[1].data.uint8[7] = text[6];
  
  packets[2].id = 0x121;
  packets[2].rtr = 0;
  packets[2].length = 8;
  packets[2].data.uint8[0] = 0x22;
  packets[2].data.uint8[1] = text[7];
  packets[2].data.uint8[2] = text[8];
  packets[2].data.uint8[3] = text[9];
  packets[2].data.uint8[4] = text[10];
  packets[2].data.uint8[5] = text[11];
  packets[2].data.uint8[6] = text[12];
  packets[2].data.uint8[7] = text[13];
   
  
  for(i = 0; i < 4; i++) {
    CAN0.sendFrame(packets[i]);
    Serial.println("message[");
    Serial.println(i);
    Serial.println("] sended:");
    printFrame2(packets[i]);
    delay(1); 
  }
}


SerialCommand sCmd; // The SerialCommand object
// put function declarations here:
// int myFunction(int, int);
 
// This gets set as the default handler, and gets called when no other command matches.
void unrecognized(const char *command)
{
  Serial.println(F("What?"));
  Serial.print(F("Unrecognized command: "));
  Serial.println(command); // Write the unrecognized command to Serial
  Serial.println("Ready ...!");
  CAN_FRAME txFrame;
  txFrame.rtr = 0;
  txFrame.id = 0x123;
  txFrame.extended = false;
  txFrame.length = 4;
  txFrame.data.int8[0] = 0x10;
  txFrame.data.int8[1] = 0x1A;
  txFrame.data.int8[2] = 0xFF;
  txFrame.data.int8[3] = 0x5D;
  txFrame.data.int8[7] = 0xFF;
  CAN0.sendFrame(txFrame);
  Serial.println("sended some can message)");
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
void testDisp(){
  show_text("meganeUA     ");
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




// Callback function for frame with ID 0x1C1
void gotFrame_0x1C1(CAN_FRAME *packet) 
{
  printFrame(packet, 0);

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
      // case AFFA3_KEY_PAUSE:
      // Serial.println("Pause/Play");
      // bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
      // break;
      
      // case AFFA3_KEY_ROLL_UP: // Next track
      // Serial.println("Next Track");
      // bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
      // break;
      
      // case AFFA3_KEY_ROLL_DOWN: // Previous track
      // Serial.println("Previous Track");
      // bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
      // break;
      
      // case AFFA3_KEY_LOAD: // Previous track
      // Serial.println("loading some text");
      // testDisp();
      // break;
      
      default:
      Serial.print("Unknown key: 0x");
      Serial.println(key, HEX);
      break;
    }
    
  }
}
  
  
  // Callback function for frame with ID 0x151
  void gotFrame_0x3AF(CAN_FRAME *packet) {}
  // Callback function for frame with ID 0x151
  void gotFrame_0x3CF(CAN_FRAME *packet) 
  {
    printFrame(packet,-2);
    struct CAN_FRAME answer;
    
	  static int8_t timeout = AFFA3_PING_TIMEOUT;
    // IF("AFFA3"){

    // }
    // else if (condition)
    // {
      if ((packet->data.uint8[0] == 0x61) && (packet->data.uint8[1] == 0x11)) { /* Żądanie synchronizacji */
        answer.id = AFFA3_PACKET_ID_SYNC;
        answer.length = AFFA3_PACKET_LEN;
        answer.data.uint8[0] = 0x70;
        answer.data.uint8[1] = 0x1A;
        answer.data.uint8[2] = 0x11;
        answer.data.uint8[3] = 0x00;
        answer.data.uint8[4] = 0x00;
        answer.data.uint8[5] = 0x00;
        answer.data.uint8[6] = 0x00;
        answer.data.uint8[7] = 0x01;
        
        CAN0.sendFrame(answer);
        _sync_status &= ~AFFA3_SYNC_STAT_FAILED;
        if (packet->data.uint8[2] == 0x01)
          _sync_status |= AFFA3_SYNC_STAT_START;
          return;
      }
      if (packet->data.uint8[0] == 0x69) {
        _sync_status |= AFFA3_SYNC_STAT_PEER_ALIVE;
        return;
      }
        	/* Wysyłamy pakiet informujący o tym że żyjemy */
          answer.id = AFFA3_PACKET_ID_SYNC;
          answer.length = AFFA3_PACKET_LEN;
          answer.data.uint8[0] = 0x79;
          answer.data.uint8[1] = 0x00; /* Tutaj czasem pojawia się 0x01, czemu? */
          answer.data.uint8[2] = AFFA3_PACKET_FILLER;
          answer.data.uint8[3] = AFFA3_PACKET_FILLER;
          answer.data.uint8[4] = AFFA3_PACKET_FILLER;
          answer.data.uint8[5] = AFFA3_PACKET_FILLER;
          answer.data.uint8[6] = AFFA3_PACKET_FILLER;
          answer.data.uint8[7] = AFFA3_PACKET_FILLER;
          CAN0.sendFrame(answer);
	     

    if ((_sync_status & AFFA3_SYNC_STAT_FAILED) || (_sync_status & AFFA3_SYNC_STAT_START)) { /* Błąd synchronizacji */
        /* Wysyłamy pakiet z żądaniem synchronizacji */
       answer.id = 0x3DF; //affa2 3df? // AFFA3_PACKET_ID_SYNC;
       answer.length = AFFA3_PACKET_LEN;
       answer.data.uint8[0] = 0x7A;
       answer.data.uint8[1] = 0x01;
       answer.data.uint8[2] = AFFA3_PACKET_FILLER;
       answer.data.uint8[3] = AFFA3_PACKET_FILLER;
       answer.data.uint8[4] = AFFA3_PACKET_FILLER;
       answer.data.uint8[5] = AFFA3_PACKET_FILLER;
       answer.data.uint8[6] = AFFA3_PACKET_FILLER;
       answer.data.uint8[7] = AFFA3_PACKET_FILLER;
        CAN0.sendFrame(answer);
        
        _sync_status &= ~AFFA3_SYNC_STAT_START;
        delay(100);
      }
      else {
        if (_sync_status & AFFA3_SYNC_STAT_PEER_ALIVE) {
          timeout = AFFA3_PING_TIMEOUT;
          _sync_status &= ~AFFA3_SYNC_STAT_PEER_ALIVE;
        }
        else {
          timeout--;
          if (timeout <= 0) { /* Nic nie odpowiada, wymuszamy resynchronizację */
            _sync_status = AFFA3_SYNC_STAT_FAILED;
            /* Wszystkie funkcje tracą rejestracje */
            _sync_status &= ~AFFA3_SYNC_STAT_FUNCSREG;
            
            AFFA3_PRINT("ping timeout!\n");
          }
        }
      }
       
    // }
    

    
  }
  // Callback function for frame with ID 0x151
  void gotFrame_0x151(CAN_FRAME *packet) 
  {
  printFrame(packet, 1);

  
    // Extract key text from CAN message
    if (packet->data.uint8[0] == 0x21) // Start of the text payload
    {
        char displayText[9]; // 8 characters + null terminator
        memcpy(displayText, &packet->data.uint8[1], 8); // Copy text data
        displayText[8] = '\0'; // Null terminate the string
        bool isAUX = 
          (packet->data.uint8[1] == 'A') &&
          (packet->data.uint8[2] == 'U') &&
          (packet->data.uint8[3] == 'X') ;
        bool isPause= 
        
          (packet->data.uint8[2] == 'P') &&
          (packet->data.uint8[3] == 'A') &&
          (packet->data.uint8[4] == 'U') &&
          (packet->data.uint8[5] == 'S') &&
          (packet->data.uint8[6] == 'E');

        if (isAUX) 
        {
            current_in_AUX_mode = true;
            Serial.println("AUX mode enabled");
        } 
        else if (isPause) 
        {
            Serial.println("PAUSE detected, staying in AUX mode.");
        } 
        else 
        {
            current_in_AUX_mode = false;
            Serial.print("AUX mode disabled, current text: ");
            Serial.println(displayText);
        }
    }
}

// General callback function for frames that don't match the specific IDs
void gotFrame(CAN_FRAME *frame) 
{
  printFrame(frame, -1);
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


void sendCAN() {
  char* b3Str = sCmd.next();
  char* b4Str = sCmd.next();
  char* b5Str = sCmd.next();
  char* b6Str = sCmd.next();
  char* b7Str = sCmd.next();
  char* b8Str = sCmd.next();
  char* text = sCmd.next();

  if (!b6Str || !b7Str || !text) {
    Serial.println("Usage: sendCAN <byte6> <byte7> <text>");
    return;
  }

  uint8_t byte3 = strtol(b3Str, nullptr, 16);
  uint8_t byte4 = strtol(b4Str, nullptr, 16);
  uint8_t byte5 = strtol(b5Str, nullptr, 16);
  uint8_t byte6 = strtol(b6Str, nullptr, 16);
  uint8_t byte7 = strtol(b7Str, nullptr, 16);
  uint8_t byte8 = strtol(b8Str, nullptr, 16);

  String textStr = String(text);
  textStr.replace("_", " "); // use underscores for spaces
  while (textStr.length() < 14) textStr += ' ';

  CAN_FRAME answer;

  // Frame 1
  answer.id = 0x151;
  answer.length = AFFA3_PACKET_LEN; 

  answer.data.uint8[0] = 0x10;
  answer.data.uint8[1] = 0x0E;
  //10 69 21 05 FF 00 00 49  -long text setted
  answer.data.uint8[2] = byte3;// 74- full window, 77-not full. if sended not full when not applid - it fill freze at main screen.               //sc 151 2 54 3 0 0 0 0 0  to close full window
  answer.data.uint8[3] = byte4;//45= AF-RDS 55-none
  answer.data.uint8[4] = byte5; //???55 ALSWAYS
  answer.data.uint8[5] = byte6;// ADD df-MANU / FF - NOTHING  FD -PRESET ??- LIST
  answer.data.uint8[6] = byte7;//20 mid point, , 31-39 -5sym+ point +2sym+  UPPERSAD, 60-plain 8 sym, 70-79 8 sym +uppersad
  answer.data.uint8[7] = byte8;//always 1
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
  //  CAN0.setRXFilter(0, 0x1C1, 0x7FF, false);  // Catch frames with ID 0x1C1
  //  CAN0.setRXFilter(1, 0x151, 0x7FF, false);  // Catch frames with ID 0x151
  //  CAN0.setRXFilter(2, 0x3CF, 0x7FF, false);  // Catch frames with ID 0x3cF
  //  CAN0.setRXFilter(3, 0x3AF, 0x7FF, false);  // Catch frames with ID 0x3cF
  
  // // Register callback functions for specific IDs
  // CAN0.setCallback(0, gotFrame_0x1C1);  // Frame with ID 0x1C1 will trigger this callback
  // CAN0.setCallback(1, gotFrame_0x151);  // Frame with ID 0x151 will trigger this callback
  // CAN0.setCallback(2, gotFrame_0x3CF);  // Frame with ID 0x151 will trigger this callback
  // CAN0.setCallback(3, gotFrame_0x3AF);  // Frame with ID 0x151 will trigger this callback
  
  // // General callback function for any other frame that doesn't match the above filters
  CAN0.watchFor();
  //CAN0.setGeneralCallback(gotFrame);
 




  //Serial.println(" CAN............500Kbps");

  // // Setup callbacks for SerialCommand commands
  sCmd.addCommand("t", testDisp);         // print test message on display
  sCmd.addCommand("p", onPressCommand);
  sCmd.addCommand("sendCAN", sendCAN);
  sCmd.addCommand("sc", sendGenericCAN);

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

  //affa3_init();
 
} 
 
 
#define SYNC_INTERVAL_MS 500
static uint32_t last_sync = 0;

void affa3_tick2(void) {
	struct CAN_FRAME packet;
	static int8_t timeout = AFFA3_PING_TIMEOUT;
	
	/* Wysyłamy pakiet informujący o tym że żyjemy */
	packet.id = AFFA3_PACKET_ID_SYNC;
	packet.length = AFFA3_PACKET_LEN;
	packet.data.uint8[0] = 0x79;
	packet.data.uint8[1] = 0x00; /* Tutaj czasem pojawia się 0x01, czemu? */
	packet.data.uint8[2] = AFFA3_PACKET_FILLER;
	packet.data.uint8[3] = AFFA3_PACKET_FILLER;
	packet.data.uint8[4] = AFFA3_PACKET_FILLER;
	packet.data.uint8[5] = AFFA3_PACKET_FILLER;
	packet.data.uint8[6] = AFFA3_PACKET_FILLER;
	packet.data.uint8[7] = AFFA3_PACKET_FILLER;
	CAN0.sendFrame(packet);
	if ((_sync_status & AFFA3_SYNC_STAT_FAILED) || (_sync_status & AFFA3_SYNC_STAT_START)) { /* Błąd synchronizacji */
		/* Wysyłamy pakiet z żądaniem synchronizacji */
		packet.id = AFFA3_PACKET_ID_SYNC;
		packet.length = AFFA3_PACKET_LEN;
		packet.data.uint8[0] = 0x7A;
		packet.data.uint8[1] = 0x01;
		packet.data.uint8[2] = AFFA3_PACKET_FILLER;
		packet.data.uint8[3] = AFFA3_PACKET_FILLER;
		packet.data.uint8[4] = AFFA3_PACKET_FILLER;
		packet.data.uint8[5] = AFFA3_PACKET_FILLER;
		packet.data.uint8[6] = AFFA3_PACKET_FILLER;
		packet.data.uint8[7] = AFFA3_PACKET_FILLER;
    CAN0.sendFrame(packet);
		
		_sync_status &= ~AFFA3_SYNC_STAT_START;
		delay(100);
	}
	else {
		if (_sync_status & AFFA3_SYNC_STAT_PEER_ALIVE) {
			timeout = AFFA3_PING_TIMEOUT;
			_sync_status &= ~AFFA3_SYNC_STAT_PEER_ALIVE;
		}
		else {
			timeout--;
			if (timeout <= 0) { /* Nic nie odpowiada, wymuszamy resynchronizację */
				_sync_status = AFFA3_SYNC_STAT_FAILED;
				/* Wszystkie funkcje tracą rejestracje */
				_sync_status &= ~AFFA3_SYNC_STAT_FUNCSREG;
				
				Serial.println("ping timeout!\n");
			}
		}
	}
	
	
}



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
 

