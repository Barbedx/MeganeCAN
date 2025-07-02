#include <Arduino.h>  
#include <SerialCommand.h> // Assuming this is already included in your project 
#include "display/Affa3Display.h"
#include "effects/ScrollEffect.h" // Assuming this is already included in your project

SerialCommand sCmd; // The SerialCommand object
Affa3Display display; // Create an instance of Affa3Display
bool sessionStarted = false;
unsigned long lastPingTime = 0;


void gotFrame(CAN_FRAME *frame)
{

    // if ((frame->id == 0x521 || frame->id == 0x3DF) && affa3_is_synced)
    // { // didnt process after sync?
    //     // Skip sync frames answers in radio mode becouse we generate it by ourselves

    //     // return;
    // }

    if (/*affa3_is_synced &&*/ !(

                               (frame->id == 0x3CF && frame->data.uint8[0] == 0x69) ||
                               (frame->id == 0x521 || frame->id == 0x3DF)))
    {
    }
    CanUtils::printCanFrame(*frame, false);

    display.recv(frame);
    // Echo or other processing can be added here
}



void cmd_enable() { display.setState(true); }
void cmd_disable() { display.setState(false);  }
// void cmd_enable()    { affa3_display_ctrl(0x01) displayManager.enableDisplay(); }



// void cmd_messageTestold5() { displayManager.messageTest5(); }
//  void cmd_messageTestold6() { displayManager.messageTest6(); }
//  void cmd_messageTestold(){ displayManager.messageTest2(); }
//   void cmd_mgwelcome(){ displayManager.messageWelcome(); }
void cmd_scrollmtx()
{
    const char *text = sCmd.next();
    const char *delayStr = sCmd.next();

    if (!text)
    {
       // AFFA3_PRINT("Usage: ms <text> [delay_ms]\n");
        return;
    }

    uint16_t delayMs = 300; // default delay
    if (delayStr)
    {
        delayMs = atoi(delayStr);
        if (delayMs < 20)
            delayMs = 20; // clamp minimum
    }
    ScrollEffect(&display, ScrollDirection::Right, text, delayMs);

    //display.scrollText(text, delayMs);
}

void cmd_scrollmtxl()
{
    const char *text = sCmd.next();
    const char *delayStr = sCmd.next();

    if (!text)
    {
       // AFFA3_PRINT("Usage: ms <text> [delay_ms]\n");
        return;
    }

    uint16_t delayMs = 300; // default delay
    if (delayStr)
    {
        delayMs = atoi(delayStr);
        if (delayMs < 20)
            delayMs = 20; // clamp minimum
    }
    ScrollEffect(&display, ScrollDirection::Left, text, delayMs); 
}
 
void cmd_setTime()
{

    char *timeStr = sCmd.next(); // e.g., "0930"
    if (!timeStr)
    {
        Serial.println("Usage: st <HHMM>");
        return;
    }
    display.setTime(timeStr); // unknown protocol
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("------------------------");
    Serial.println("   MEGANE CAN BUS       ");
    Serial.println("------------------------");

    // Setup commands
    sCmd.addCommand("e", cmd_enable);
    sCmd.addCommand("d", cmd_disable);
//    sCmd.addCommand("mtx", cmd_mtx);    // workss
    sCmd.addCommand("st", cmd_setTime); // Example: st 0925
    sCmd.addCommand("msr", cmd_scrollmtx); // Example: st 0925
    sCmd.addCommand("msl", cmd_scrollmtxl); // Example: st 0925

    // CAN0.setCANPins(GPIO_NUM_5, GPIO_NUM_4); // Set CAN RX/TX pins
    CAN0.setCANPins(GPIO_NUM_3, GPIO_NUM_4); // Set CAN RX/TX pins
    CAN0.begin(CAN_BPS_500K);                // 500 Kbps bitrate

    CAN0.setGeneralCallback(gotFrame);

    CAN0.watchFor();

    Serial.println(" CAN...............INIT");
}

#define SYNC_INTERVAL_MS 500
static uint32_t last_sync = 0;

void loop()
{
    sCmd.readSerial();
    uint32_t now = millis();
    if (now - last_sync > SYNC_INTERVAL_MS)
    { 
        last_sync = now;
    }
}
