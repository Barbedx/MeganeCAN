#include <Arduino.h>
#include "effects/ScrollEffect.h" // Assuming this is already included in your project
#include <stdlib.h>               // for rand()
#include "server/HttpServerManager.h"
#include <secrets.h>
#include <WiFi.h>
#include <ElegantOTA.h>
#include <SerialCommands.h> // Assuming this is already included in your project
// #include <PsychicHttp.h>


#include "ElmManager/MyELMManager.h"
#include "display/Affa3/Affa3Display.h"
#include "display/Affa3Nav/Affa3NavDisplay.h"
 

// SerialCommand sCmd;   // The SerialCommand object
// Affa3NavDisplay display; // Create an instance of Affa3Display
AffaDisplayBase* display = nullptr;
bool sessionStarted = false;
unsigned long lastPingTime = 0;

Preferences preferences;
// HttpServerManager serverManager(*display, preferences);
HttpServerManager* serverManager = nullptr;
MyELMManager * elmManager = nullptr;


// Enter your WIFI credentials in secret.h
const char *ssid = Soft_AP_WIFI_SSID;
const char *password = Soft_AP_WIFI_PASS;

const char* ELM_SSID = "Vgate"; 

void gotFrame(CAN_FRAME *frame)
{


    

    if(frame->id != 0x3CF && frame->id !=0x3AF && frame->id != 0x7AF)
        CanUtils::printCanFrame(*frame, false);
    display->recv(frame);
    // Echo or other processing can be added here
}

void cmd_enable(SerialCommands* sender) { 
    Serial.println("Enabling display");
    display->setState(true); 
}
void cmd_disable(SerialCommands* sender) { display->setState(false); }
// void cmd_enable()    { affa3_display_ctrl(0x01) displayManager.enableDisplay(); }

// void cmd_messageTestold5() { displayManager.messageTest5(); }
//  void cmd_messageTestold6() { displayManager.messageTest6(); }
//  void cmd_messageTestold(){ displayManager.messageTest2(); }
//   void cmd_mgwelcome(){ displayManager.messageWelcome(); }
void cmd_scrollmtx(SerialCommands* sender)
{
    const char *text = sender->Next();
    const char *delayStr = sender->Next();

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
    ScrollEffect(display, ScrollDirection::Right, text, delayMs);

    // display.scrollText(text, delayMs);
}

void cmd_scrollmtxl(SerialCommands* sender)
{
    const char *text = sender->Next();
    const char *delayStr = sender->Next();

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
    ScrollEffect(display, ScrollDirection::Left, text, delayMs);
}

void cmd_setTime(SerialCommands* sender)
{

    char *timeStr = sender->Next(); // e.g., "0930"
    if (!timeStr)
    {
        Serial.println("Usage: st <HHMM>");
        return;
    }
    display->setTime(timeStr); // unknown protocol
}

// Declare command handlers
// void cmd_enable(SerialCommands* sender);
// void cmd_disable(SerialCommands* sender);
// void cmd_setTime(SerialCommands* sender);
// void cmd_scrollmtx(SerialCommands* sender);
// void cmd_scrollmtxl(SerialCommands* sender);

// Create command objects
SerialCommand cmd_e("e", cmd_enable);
SerialCommand cmd_d("d", cmd_disable);
SerialCommand cmd_st("st", cmd_setTime);
SerialCommand cmd_msr("msr", cmd_scrollmtx);
SerialCommand cmd_msl("msl", cmd_scrollmtxl);


// Create SerialCommands manager
char serial_command_buffer[64];
char serial_delim[] = " \r\n";
SerialCommands serialCommands(&Serial, serial_command_buffer, sizeof(serial_command_buffer), serial_delim);

void initSerial(){
    
    // Initialize random seed (run only once)
    
    Serial.begin(115200);
    delay(2000);
    Serial.println("------------------------");
    Serial.println("   MEGANE CAN BUS       ");
    Serial.println("------------------------");
    // Setup commands
    // Register commands
    serialCommands.AddCommand(&cmd_e);
    serialCommands.AddCommand(&cmd_d);
    serialCommands.AddCommand(&cmd_st);
    serialCommands.AddCommand(&cmd_msr);
    serialCommands.AddCommand(&cmd_msl);
}


void restoreDisplay(IDisplay& display, Preferences& prefs) {
    
    
    prefs.begin("display", true);  // read-only
    bool autoRestore = prefs.getBool("autoRestore", true); // default true
    if (!autoRestore) {
        prefs.end();
        Serial.println("Auto restore disabled by setting.");
        return;
    }
    Serial.println("Auto restore getted and is true.");
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


void initDisplay() {
    Preferences prefs;
    prefs.begin("config", true);
    String displayType = prefs.getString("display_type", "affa3");
    prefs.end();

    Serial.println("[Display Init] Display type from config: " + displayType);

    if (displayType == "affa3nav") {
        Serial.println("[Display Init] Instantiating Affa3NavDisplay");
        display = new Affa3NavDisplay();
    } else {
        Serial.println("[Display Init] Instantiating Affa3Display (default)");
        display = new Affa3Display();
    }
    
    display->begin();    // ✅ Only initializes BLE if needed
    serverManager = new HttpServerManager(*display, preferences);  // ✅ Moved here
    elmManager = new MyELMManager(*display);
    Serial.println("[Display Init] HttpServerManager initialized");

    // Optionally call init method if needed
    // display->init(); 
    // Serial.println("[Display Init] Display initialized");
}

void setup()
{

    // Initialize random seed (run only once)
     
    delay(2000);
    initDisplay();
    initSerial();
    // CAN0.setCANPins(GPIO_NUM_4, GPIO_NUM_5); // Set CAN RX/TX pins
    CAN0.setCANPins(GPIO_NUM_3, GPIO_NUM_4); // Set CAN RX/TX pins  MINI
    CAN0.begin(CAN_BPS_500K);                // 500 Kbps bitrate

    CAN0.setGeneralCallback(gotFrame);

    CAN0.watchFor();
    Serial.println(" CAN...............INIT");


  // Start the access point
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid, password);

    serverManager->begin();
 
        
    Serial.println(" all............inited"); // Serve the commands via HTTP GET requests
 
    Serial.println("RESTAPI........done");

    delay(2000); // Wait for CAN bus to stabilize
    restoreDisplay(*display, preferences); // Restore display state and text from NVS TODO:Fix parameter ui


 
}

#define SYNC_INTERVAL_MS 500
static uint32_t last_sync = 0;

bool elmConnected = false;

void loop()
{
    serialCommands.ReadSerial();
    ElegantOTA.loop();
     // 1️⃣ Try to connect to ELM WiFi if not connected
    if (!elmConnected) {
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.begin(ELM_SSID);
            Serial.print("Connecting to ELM WiFi...");
            delay(500); // short delay, non-blocking is possible with millis()
        } else {
            Serial.println("\nConnected to ELM WiFi");
            Serial.print("ESP32 IP in ELM network: ");
            Serial.println(WiFi.localIP());

            // Initialize ELM manager only after WiFi is ready
            elmManager->setup();
            elmConnected = true;
        }
    }
    // 2️⃣ If connected, run ELM tick
    if (elmConnected) {
        elmManager->tick();
    }
    elmManager->tick();
    display->processEvents();
    uint32_t now = millis();
    if (now - last_sync > SYNC_INTERVAL_MS)
    {
        last_sync = now;
    }
}
