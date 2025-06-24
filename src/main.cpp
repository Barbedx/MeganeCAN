#include <Arduino.h>
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
#include "CanUtils.h"
#include <SerialCommand.h> // Assuming this is already included in your project
#include "DisplayManager.h"
#include "affa3.h"

DisplayManager displayManager;

SerialCommand sCmd; // The SerialCommand object

bool sessionStarted = false;
unsigned long lastPingTime = 0;

void printCanFrame(const CAN_FRAME &frame, bool isOutgoing)
{
    const char *direction = isOutgoing ? "[TX]" : "[RX]";
    Serial.print(direction);
    Serial.print(" ID: 0x");
    if (frame.id < 0x100)
        Serial.print("0"); // pad if needed
    Serial.print(frame.id, HEX);
    Serial.print(" Len: ");
    Serial.print(frame.length);
    Serial.print(" Data: { ");
    for (int i = 0; i < frame.length; i++)
    {
        if (frame.data.uint8[i] < 0x10)
            Serial.print("0");
        Serial.print(frame.data.uint8[i], HEX);
        if (i < frame.length - 1)
            Serial.print(" ");
    }
    Serial.println(" }");
}

void gotFrame(CAN_FRAME *frame)
{
    printCanFrame(*frame, false);
    affa3_recv(frame);
    // Echo or other processing can be added here
}

void gotFrame_0x3DF(CAN_FRAME *frame)
{
    // Do nothing or add logging if needed
    printCanFrame(*frame, false);
}

void gotFrame_0x0A9(CAN_FRAME *frame)
{
    printCanFrame(*frame, false);
    unsigned char msg4a9[8] = {0x74, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
    CanUtils::sendMsgBuf(0x4A9, msg4a9);
}

void gotFrame_0x1C1(CAN_FRAME *frame)
{
    printCanFrame(*frame, false);
    unsigned char msg5c1[8] = {0x74, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
    CanUtils::sendMsgBuf(0x5C1, msg5c1);
}

void cmd_startSync() { displayManager.startSync(); }
void cmd_syncOK() { displayManager.syncOK(); }
void cmd_syncDisp() { displayManager.syncDisp(); }
void cmd_register() { displayManager.registerDisplay(); }
void cmd_init() { displayManager.initDisplay(); }
void cmd_enable() { affa3_display_ctrl(0x02); }
void cmd_disable() { affa3_display_ctrl(0x00); }
// void cmd_enable()    { affa3_display_ctrl(0x01) displayManager.enableDisplay(); }
void cmd_messageTestold(){   displayManager.messageTest(); }
//void cmd_messageTestold(){ displayManager.messageTest2(); }
// void cmd_mgwelcome(){ displayManager.messageWelcome(); }
void cmd_messageTest()
{
    char oldStr[8] = {'1', '2', '3', '4', '5', '6', '7', '8'};
    char newStr[12] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};

    affa3_do_set_text(0xFF, 0x00, 0x00, 0x00, oldStr, newStr);
}
void cmd_messageTest2()
{
    char oldStr[8] = {'T', 'E', 'S', 'T', '1', ' ', ' ', ' '};
    char newStr[12] = {'H', 'e', 'l', 'l', 'o', ' ', 'C', 'A', 'N', '!', ' ', 0};

    affa3_do_set_text(0xFF, 0x00, 0x00, 0x00, oldStr, newStr);
}
void cmd_mgwelcome()
{
    char oldStr[8] = {'W', 'E', 'L', 'C', 'O', 'M', 'E', '!'};
    char newStr[12] = {'H', 'e', 'l', 'l', 'o', ' ', 'C', 'A', 'N', '!', ' ', 0};

    affa3_do_set_text(0xFF, 0x00, 0x00, 0x01, oldStr, newStr);
}

void gotFrame_0x3CF(CAN_FRAME *frame)
{
    printCanFrame(*frame, false);
    displayManager.handleFrame_0x3CF(frame);
}

void setup()
{
    Serial.begin(115200);
    Serial.println("------------------------");
    Serial.println("   MEGANE CAN BUS       ");
    Serial.println("------------------------");

    // Setup commands
    sCmd.addCommand("ss", cmd_startSync);
    sCmd.addCommand("so", cmd_syncOK);
    sCmd.addCommand("sd", cmd_syncDisp);
    sCmd.addCommand("r", cmd_register);
    sCmd.addCommand("i", cmd_init);
    sCmd.addCommand("e", cmd_enable);
    sCmd.addCommand("d", cmd_disable);
    sCmd.addCommand("mt", cmd_messageTest);
    sCmd.addCommand("mto", cmd_messageTestold); // works
    sCmd.addCommand("mt2", cmd_messageTest2); //not working
    sCmd.addCommand("mw", cmd_mgwelcome);

    CAN0.setCANPins(GPIO_NUM_5, GPIO_NUM_4); // Set CAN RX/TX pins
    CAN0.begin(CAN_BPS_500K);                // 500 Kbps bitrate

    // CAN0.setRXFilter(0, 0x1C1, 0x7FF, false);
    // CAN0.setCallback(0, gotFrame_0x1C1);

    // CAN0.setRXFilter(1, 0x0A9, 0x7FF, false);
    // CAN0.setCallback(1, gotFrame_0x0A9);

    // CAN0.setRXFilter(2, 0x3DF, 0x7FF, false);
    // CAN0.setCallback(2, gotFrame_0x3DF);

    // CAN0.setRXFilter(3, 0x3CF, 0x7FF, false);
    // CAN0.setCallback(3, gotFrame_0x3CF);

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
        //    displayManager.syncOK();
        affa3_tick();
        last_sync = now;
    }
}
