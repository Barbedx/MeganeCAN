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

    if ((frame->id == 0x521 || frame->id == 0x3DF) && affa3_is_synced)
    { // didnt process after sync?
        // Skip sync frames answers in radio mode becouse we generate it by ourselves

        // return;
    }

    if (affa3_is_synced && !(

                               (frame->id == 0x3CF && frame->data.uint8[0] == 0x69) ||
                               (frame->id == 0x521 || frame->id == 0x3DF)))
    {
        printCanFrame(*frame, false);
    }

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
void cmd_messageTestold() { 
    affa3_old_set_text("12345678"); }
void cmd_messageTestold2() { affa3_old_scroll_text("12233344445555", 300) ; }
void cmd_messageTestold3() { affa3_old_scroll_text("welcome back, Igor!", 300) ;}
void cmd_messageTestold4() { affa3_old_scroll_text("nice to see you again!", 300) ; }
void cmd_messageTestold5() { displayManager.messageTest5(); }
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
        AFFA3_PRINT("Usage: ms <text> [delay_ms]\n");
        return;
    }

    uint16_t delayMs = 300; // default delay
    if (delayStr)
    {
        delayMs = atoi(delayStr);
        if (delayMs < 20)
            delayMs = 20; // clamp minimum
    }

    affa3_old_scroll_text(text, delayMs);
}
void cmd_mtx()
{
    char *text = sCmd.next();
    affa3_old_set_text(text);
}

void setTime(const char *clock)
{
    if (strlen(clock) != 4 || !isdigit(clock[0]) || !isdigit(clock[1]) ||
        !isdigit(clock[2]) || !isdigit(clock[3]))
    {
        Serial.println("Invalid time format. Use 4-digit HHMM.");
        return;
    }

    CAN_FRAME answer;
    answer.id = AFFA3_PACKET_ID_DISPLAY_CTRL;
    answer.length = 8;
    answer.data.uint8[0] = 0x05;
    answer.data.uint8[1] = 'V'; // likely constant
    answer.data.uint8[2] = clock[0];
    answer.data.uint8[3] = clock[1];
    answer.data.uint8[4] = clock[2];
    answer.data.uint8[5] = clock[3];
    answer.data.uint8[6] = 0x00;
    answer.data.uint8[7] = 0x00;

    CAN0.sendFrame(answer);
    Serial.print("Sent time set frame  : ");
    printCanFrame(answer, true);
    Serial.println(clock);
}
void cmd_setTime()
{

    char *timeStr = sCmd.next(); // e.g., "0930"
    if (!timeStr)
    {
        Serial.println("Usage: st <HHMM>");
        return;
    }
    setTime(timeStr); // unknown protocol
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
    sCmd.addCommand("mto", cmd_messageTestold);   // works
    sCmd.addCommand("mto2", cmd_messageTestold2); // works
    sCmd.addCommand("mto3", cmd_messageTestold3);
    sCmd.addCommand("mto4", cmd_messageTestold4);
    sCmd.addCommand("mto5", cmd_messageTestold5);
    sCmd.addCommand("mtx", cmd_mtx);    // workss
    sCmd.addCommand("st", cmd_setTime); // Example: st 0925
    sCmd.addCommand("ms", cmd_scrollmtx); // Example: st 0925

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
        // affa3_tick();
        last_sync = now;
    }
}
