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

    // Skip known spam or echo frames
//   if (affa3_is_synced) {
//         // Skip known AFFA3 protocol keep-alive and echo messages
//         if ((frame->id == 0x3CF && frame->data.uint8[0] == 0x69) || 
//             (frame->id == 0x3DF)) 
//         {
//             return;
//         }
//     }

    printCanFrame(*frame, false);

    affa3_recv(frame);
    // Echo or other processing can be added here
}

void cmd_startSync() { displayManager.startSync(); }
void cmd_syncOK() { displayManager.syncOK(); }
void cmd_syncDisp() { displayManager.syncDisp(); }
void cmd_register() { displayManager.registerDisplay(); }
void cmd_init() { displayManager.initDisplay(); }
void cmd_enable() { affa3_display_ctrl(0x02); }
void cmd_disable() { affa3_display_ctrl(0x00); }
// void cmd_enable()    { affa3_display_ctrl(0x01) displayManager.enableDisplay(); }
void cmd_messageTestold() { displayManager.messageTest(); }
// void cmd_messageTestold(){ displayManager.messageTest2(); }
//  void cmd_mgwelcome(){ displayManager.messageWelcome(); }

void cmd_mtx()
{
    char *arg1 = sCmd.next(); // textType (hex)
    char *arg2 = sCmd.next(); // chan (hex)
    char *arg3 = sCmd.next(); // loc (hex)
    char *arg4 = sCmd.next(); // oldText (8 chars)

    if (!arg1 || !arg2 || !arg3 || !arg4 || strlen(arg4) != 8)
    {
        Serial.println("Usage: mtx <textType hex> <chan hex> <loc hex> <8char_text>");
        return;
    }

    uint8_t textType = strtol(arg1, NULL, 16);
    uint8_t chan = strtol(arg2, NULL, 16);
    uint8_t loc = strtol(arg3, NULL, 16);

    char oldText[8];
    memcpy(oldText, arg4, 8);

    affa3_old_set_text(textType, chan, loc, oldText);
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
    sCmd.addCommand("mto", cmd_messageTestold); // works
    sCmd.addCommand("mtx", cmd_mtx);            // works

    CAN0.setCANPins(GPIO_NUM_5, GPIO_NUM_4); // Set CAN RX/TX pins
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
        //    displayManager.syncOK();
        affa3_tick();
        last_sync = now;
    }
}
