#include <Arduino.h>
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
 
// #include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
// Must be before #include <ESP_WiFiManager_Lite.h>
#include <SerialCommand.h> // Assuming this is already included in your project 



unsigned char msg5c1[8] = {0x74, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
unsigned char msg4a9[8] = {0x74, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};

uint8_t pingMsg[] = {
  'y', 
  0x00,
  0x81,
  0x81,
  0x81,
  0x81,
  0x81,
  0x81,
}; 

void printFrame_inline(CAN_FRAME &frame, bool out)
{
  // Print message
  Serial.print("ID: ");
  Serial.print(frame.id, HEX);

  Serial.print(
    out ? " OUT " : "  IN " // Print OUT if out is true, otherwise print IN
  );
  Serial.print("Len: ");
  Serial.print(frame.length, DEC);
  Serial.print("{");

  for (int i = 0; i < frame.length; i++)
  {
    Serial.print(frame.data.uint8[i], HEX);
    Serial.print(" ");
  }
  Serial.println("}");
}

//
SerialCommand sCmd; // The SerialCommand object
// put function declarations here:
// int myFunction(int, int); 
    

    void sendCan(uint32_t id, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
        uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7, bool silent = false) {
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
      if (!silent )
      { 
        printFrame_inline(frame, true); // Print the frame after sending
      }
      
      }
      

void sendMsgBuf(uint32_t id, uint8_t* data, uint8_t len = 8, bool silent =false) {
    if (len != 8) return; // Only support 8-byte frames for now

    sendCan(id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], silent);
}
 

void startSync() {
  unsigned char startSyncMsg[8] = {0x7A, 0x01, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
  sendMsgBuf(0x3DF,  startSyncMsg);    
} 
void syncOK() {
  sendMsgBuf(0x3DF,  pingMsg, 8, true);
}

void syncDisp() {
  unsigned char syncDispMsg[8] = {0x70, 0x1A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01};
  sendMsgBuf(0x3DF,  syncDispMsg);
}

void registerDisplay() {
  unsigned char registerDispMsg[8] = {0x70, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
  sendMsgBuf(0x1B1, registerDispMsg);
}

void enableDisplay() {
  unsigned char enableDispMsg[8] = {0x04, 0x52, 0x02, 0xFF, 0xFF, 0x81, 0x81, 0x81};
  sendMsgBuf(0x1B1, enableDispMsg);
}

void initDisplay() {
  unsigned char initDispMsg[8] = {0x70, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
  sendMsgBuf(0x121, initDispMsg);
}
 
void gotFrame(CAN_FRAME *frame) {
    printFrame_inline(*frame,false);
    //sendMsgBuf(frame->id | 0x400, frame->data.uint8, frame->length); // Echo the received frame with reply flag set
    // Here you can add code to show something on your display if needed
}
 bool sessionStarted = false;
unsigned long lastPingTime = 0;

 

void messageTest() {
  unsigned char msg11[8] = {0x10, 0x19, '~', 'q', 0x01, '1', '2', '3'};
  unsigned char msg12[8] = {0x21, '4', '5', '6', '7', '8', 0x10, ' '}; 
  unsigned char msg13[8] = {0x22, 'E', 'U', 'R', 'O', 'P', 'E', '-'};
  unsigned char msg14[8] = {0x23, '2', ' ', 'P', '1', 0x00, 0x81, 0x81};

  sendMsgBuf(0x121, msg11, 8);
  delay(1);
  sendMsgBuf(0x121, msg12, 8);
  delay(1);
  sendMsgBuf(0x121, msg13, 8);
  delay(1);
  sendMsgBuf(0x121, msg14, 8);
  delay(1);
}


 

// Callback function for frame with ID 0x151
// void gotFrame_0x3AF(CAN_FRAME *packet) {}

// You can also declare your ping timeout threshold if needed 
void gotFrame_0x3CF(CAN_FRAME *frame) {  

  printFrame_inline(*frame,false);

  struct CAN_FRAME answer;

  // IF("AFFA3"){

    // Auth request from display 3df for affa2???
  if (frame->data.uint8[0] == 0x61 && frame->data.uint8[1] == 0x11)
  {
    //sendCan(0x3DF, 0x70, 0x1A, 0x11, 0, 0, 0, 0, 0);
    // sendCan(0x3DF, 0x7A, 0x01, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81);
    // sendCan(0x3DF, 0x79, 0, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81);
    sendCan(0x3DF, 0x70, 0x1A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01);
    delay(50);
    registerDisplay(); // Register display

    delay(50);
    initDisplay(); // Init display
    delay(50);  
    // sendCan(0x3DF, 0xB0, 0x14, 0x11, 0x00, 0x1F, 0x00, 0x00, 0x00);
        
    // sendCan(0x121, 0x70, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81);
    // sendCan(0x1b1, 0x70, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81);
 
    sessionStarted = true;

    delay(50);
  }

  if (frame->data.uint8[0] == 0x69 && (frame->data.uint8[1] == 0x00 || frame->data.uint8[1] == 0x01))
  {
    // Ping
     sendCan(0x3DF, 0x79, 0, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81);
  }
  // Periodic keep-alive
  if (sessionStarted && millis() - lastPingTime > 5000)
  {
    //this is nonsense inside filtered callback, maybe it should be removed
         sendCan(0x3DF, 0x79, 0, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81);
     lastPingTime = millis();
  }
  
}
 
void gotFrame_0x3DF(CAN_FRAME *frame) {   
  //do nothing
} 
void gotFrame_0x0A9(CAN_FRAME *frame) {   
  sendMsgBuf(0x4A9, msg4a9); // Echo the received frame with reply flag set
} 
void gotFrame_0x1C1(CAN_FRAME *frame) {   
  //check the frame 
   sendMsgBuf(0x5C1, msg5c1); // Echo the received frame with reply flag set
}  

void setup()
{

  Serial.begin(115200);

  Serial.println("------------------------");
  Serial.println("   MEGANE CAN BUS       ");
  Serial.println("------------------------");
  // Setup callbacks for SerialCommand commands
 
  sCmd.addCommand("ss", startSync);       //
  sCmd.addCommand("so", syncOK);          //
  sCmd.addCommand("sd", syncDisp);          //
  sCmd.addCommand("r", registerDisplay);  //
  sCmd.addCommand("i", initDisplay);    // 
  sCmd.addCommand("e", enableDisplay);    // 
  sCmd.addCommand("mt", messageTest);  //
  //sCmd.addCommand("m", messageTextIcons);  //
 



  
  CAN0.setCANPins(GPIO_NUM_5, GPIO_NUM_4); // config for shield v1.3+, see important note above!
  CAN0.begin(CAN_BPS_500K);                // 500Kbps 
  
  //CAN0.setRXFilter(0, 0x3CF, 0x7FF, false);  
  //CAN0.setCallback(0, gotFrame_0x3CF);
  
  CAN0.setRXFilter(1, 0x1c1, 0x7FF, false);  
  CAN0.setCallback(1, gotFrame_0x1C1); // Frame with ID 0x1C1 will trigger this callback
  
  CAN0.setRXFilter(2, 0x0A9, 0x7FF, false);  
  CAN0.setCallback(2, gotFrame_0x0A9); // Frame with ID 0x151 will trigger this callback
  
  CAN0.setRXFilter(3, 0x3DF, 0x7FF, false);   // my anwer, dont need it incallback
  CAN0.setCallback(3, gotFrame_0x3DF); // Frame with ID 0x3CF will trigger this callback
  
  // // Register callback functions for specific IDs

  // // General callback function for any other frame that doesn't match the above filters
  CAN0.setGeneralCallback(gotFrame);
  CAN0.watchFor();
 
  Serial.println(" CAN...............INIT");
}

#define SYNC_INTERVAL_MS 500
static uint32_t last_sync = 0;

void loop()
{
  sCmd.readSerial();

 
  // ESPAsync_WiFiManager->run();
  uint32_t now = millis(); // Or your system time function

  if (now - last_sync > SYNC_INTERVAL_MS)
  {
    // affa3_tick2(); 
    syncOK(); // every 100ms to 1sec
    last_sync = now;
  }
  // Add more periodic logic if needed
}
