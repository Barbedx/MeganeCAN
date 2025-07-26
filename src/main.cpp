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
    sCmd.addCommand("msl", cmd_scrollmtxl); // Example: st 1111

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

//     server.listen(80);
//     server.on("/help", HTTP_GET, [](PsychicRequest *request)
//               {
//         String helpText = 
//         "Available Commands:\n\n"
//         "/settext?text=<your_text>\n"
//         "  - Displays the given text on the screen and store it to memory.\n"
//         "  - Example: /showtext?text=HelloWorld\n\n";
//         "/scrolltext?text=<your_text>\n"
//         "  - Displays the given text on the screen scrolled.\n"
//         "  - Example: /scrolltext?text=HelloWorld\n\n";
//         "/setwelcometext?text=<your_text>\n"
//         "  - set welcominmg message scrolled.\n"
//         "  - Example: /scrolltext?text=HelloWorld\n\n";
//         "/gettext\n"
//         "  - get base message text.\n"
//         "  - Example: /scrolltext?text=HelloWorld\n\n";
// ;
    
//         return request->reply(200, "text/plain", helpText.c_str()); });

//     server.on("/settext", HTTP_GET, [](PsychicRequest *request)
//               {
//         if (request->hasParam("text")) {
//         String text = request->getParam("text")->value();
//         display.setState(true); // Ensure the display is enabled
//         display.setText(text.c_str()); // Set the text on the display 
        
//         // Save to NVS
//         preferences.begin("display", false);
//         preferences.putString("lastText", text);
//         preferences.end();


//         String response = "Text shown: " + text;
//         return request->reply(200, "text/plain", response.c_str());
//         } else {

//         return request->reply(400, "text/plain", "Missing 'text' parameter");
//         } });
//     Serial.println(" showtext............inited"); // Serve the commands via HTTP GET requests

//     server.on("/setwelcometext", HTTP_GET, [](PsychicRequest *request)
//               {
//         if (request->hasParam("text")) {
//         String text = request->getParam("text")->value();
 
//         // Save to NVS
//         preferences.begin("display", false);
//         preferences.putString("welcomeText", text);
//         preferences.end();

//         display.setState(true); // Ensure the display is enabled
        
//         ScrollEffect(&display, ScrollDirection::Left,text.c_str(),250); // Set the text on the display 


//         String response = "Text saved for welcoming: " + text;
//         return request->reply(200, "text/plain", response.c_str());
//         } else {

//         return request->reply(400, "text/plain", "Missing 'text' parameter");
//         } });
//     Serial.println(" showtext............inited"); // Serve the commands via HTTP GET requests

//     server.on("/scrolltext", HTTP_GET, [](PsychicRequest *request)
//               {
//         if (request->hasParam("text")) {
//         String text = request->getParam("text")->value();
//         display.setState(true); // Ensure the display is enabled
        
//         ScrollEffect(&display, ScrollDirection::Left,text.c_str(),250); // Set the text on the display 
//         String response = "Text shown: " + text;
//         return request->reply(200, "text/plain", response.c_str());
//         } else {

//         return request->reply(400, "text/plain", "Missing 'text' parameter");
//         } });
//     Serial.println(" scrolltext............inited"); // Serve the commands via HTTP GET requests

//     server.on("/gettext", HTTP_GET, [](PsychicRequest *request)
//               {
//         preferences.begin("display", true);
//         String savedText = preferences.getString("lastText", "");
//         preferences.end();

//         if (savedText.length() > 0) {
//             String response = "Text shown: " + savedText;
//             return request->reply(200, "text/plain", response.c_str());
//         } else {
//             return request->reply(404, "text/plain", "No text stored");
//         } });

        
    Serial.println(" all............inited"); // Serve the commands via HTTP GET requests

    // server.on("/ip", [](PsychicRequest *request)
    // {
    //   String output = "Your IP is: " + request->client()->remoteIP().toString();
    //   return request->reply(output.c_str());
    // });

    Serial.println("RESTAPI........done");

    delay(2000); // Wait for CAN bus to stabilize

    // preferences.begin("display", true); // read-only
    // String savedText = preferences.getString("lastText", "");
    // String welcomeText = preferences.getString("welcomeText", "");
    // preferences.end();

    // // Initialize the display
    // display.setState(true); // Enable the display
    // if(welcomeText.length() > 0)
    // {
    //     Serial.print("Restoring welcome text: ");
    //     Serial.println(welcomeText); 
    //     ScrollEffect(&display, ScrollDirection::Left, welcomeText.c_str(), 250);
    // }
    // else{
    //     ScrollEffect(&display, ScrollDirection::Left, "                  Welcome to MEGANE 2", 250);
    // }

    // if (savedText.length() > 0)
    // {
    //     Serial.print("Restoring saved text: ");
    //     Serial.println(savedText);
    //     display.setText(savedText.c_str());
    // }
    // else
    // {
    //     if (rand() % 2 == 0)
    //     {
    //         display.setText("MEGANE");
    //     }
    //     else
    //     {
    //         display.setText("RENAULT");
    //     }
    // }
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
