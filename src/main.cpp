#include <Arduino.h>
#include <SerialCommand.h> // Assuming this is already included in your project
#include "effects/ScrollEffect.h" // Assuming this is already included in your project
#include <stdlib.h>               // for rand()
#include "server/HttpServerManager.h"
#include <secrets.h>
#include <WiFi.h>

// #include <PsychicHttp.h>


#include "display/Affa3/Affa3Display.h"
#include "display/Affa3Nav/Affa3NavDisplay.h"
 

SerialCommand sCmd;   // The SerialCommand object
Affa3NavDisplay display; // Create an instance of Affa3Display
bool sessionStarted = false;
unsigned long lastPingTime = 0;

Preferences preferences;
HttpServerManager serverManager(display, preferences);


// Enter your WIFI credentials in secret.h
const char *ssid = Soft_AP_WIFI_SSID;
const char *password = Soft_AP_WIFI_PASS;

void gotFrame(CAN_FRAME *frame)
{


    

    
    CanUtils::printCanFrame(*frame, false);
    display.recv(frame);
    // Echo or other processing can be added here
}

void cmd_enable() { 
    Serial.println("Enabling display");
    display.setState(true); 
}
void cmd_disable() { display.setState(false); }
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
    Serial.println("Scrolling text: ");
    Serial.println(text);
    ScrollEffect(&display, ScrollDirection::Right, text, delayMs);

    // display.scrollText(text, delayMs);
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

void initSerial(){
    
    // Initialize random seed (run only once)
    
    Serial.begin(115200);
    delay(2000);
    Serial.println("------------------------");
    Serial.println("   MEGANE CAN BUS       ");
    Serial.println("------------------------");
    // Setup commands
    sCmd.addCommand("e", cmd_enable);
    sCmd.addCommand("d", cmd_disable);
    //    sCmd.addCommand("mtx", cmd_mtx);    // workss
    sCmd.addCommand("st", cmd_setTime);     // Example: st 0925
    sCmd.addCommand("msr", cmd_scrollmtx);  // Example: msr 1241235134513245
    sCmd.addCommand("msl", cmd_scrollmtxl); // Example: st 2222 msl testing

}


void restoreDisplay(IDisplay& display, Preferences& prefs) {
    
    
    bool autoRestore = prefs.getBool("autoRestore", true); // default true
    if (!autoRestore) {
        prefs.end();
        Serial.println("Auto restore disabled by setting.");
        return;
    }

    prefs.begin("display", true);  // read-only
    String savedText = prefs.getString("lastText", "");
    String welcomeText = prefs.getString("welcomeText", "");
    prefs.end();

    display.setState(true);
    if (welcomeText.length() > 0) {
        ScrollEffect(&display, ScrollDirection::Left, welcomeText.c_str(), 250);
    } else {
        ScrollEffect(&display, ScrollDirection::Left, "                  Welcome to MEGANE 2", 250);
    }

    if (savedText.length() > 0) {
        display.setText(savedText.c_str());
    } else {
        if (random(0, 2) == 0) {
            display.setText("MEGANE");
        } else {
            display.setText("RENAULT");
        }
   }
}


void setup()
{

    // Initialize random seed (run only once)
     
    delay(2000);
    initSerial();
    // CAN0.setCANPins(GPIO_NUM_4, GPIO_NUM_5); // Set CAN RX/TX pins
    CAN0.setCANPins(GPIO_NUM_3, GPIO_NUM_4); // Set CAN RX/TX pins  MINI
    CAN0.begin(CAN_BPS_500K);                // 500 Kbps bitrate

    CAN0.setGeneralCallback(gotFrame);

    CAN0.watchFor();
    Serial.println(" CAN...............INIT");


  // Start the access point
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);

    serverManager.begin();
 
        
    Serial.println(" all............inited"); // Serve the commands via HTTP GET requests
 
    Serial.println("RESTAPI........done");

    delay(2000); // Wait for CAN bus to stabilize
    restoreDisplay(display, preferences); // Restore display state and text from NVS TODO:Fix parameter ui
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
