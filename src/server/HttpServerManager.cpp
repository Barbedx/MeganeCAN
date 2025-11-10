#include "HttpServerManager.h"
#include "effects/ScrollEffect.h"
#include "../commands/DisplayCommands.h"
#include <ElegantOTA.h>

HttpServerManager::HttpServerManager(IDisplay &display, Preferences &prefs) : _server(),
                                                                              _display(display),
                                                                              _prefs(prefs),
                                                                              _commands(display, prefs)
{
}

const char *htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head><title>Display Control</title>
<script>
async function loadConfig() {
  try {
    // Fetch autoRestore
    const restoreRes = await fetch('/config/restore');
    const autoRestore = await restoreRes.text();
    document.getElementById('autoRestoreCheckbox').checked = (autoRestore === '1');

    // Fetch lastText
    const lastTextRes = await fetch('/getlasttext');
    const lastText = await lastTextRes.text();
    document.getElementById('staticTextInput').value = lastText;

    // Fetch welcomeText
    const welcomeTextRes = await fetch('/getwelcometext');
    const welcomeText = await welcomeTextRes.text();
    document.getElementById('welcomeTextInput').value = welcomeText;
  } catch (e) {
    console.error('Failed to load config', e);
  }
}

window.addEventListener('DOMContentLoaded', loadConfig);
</script>


</head>
<body>
    <h1>Display Control v0.4</h1>
  <h2>display type</h2>

    <form action="/setDisplay" method="POST">
    <select name="type">
        <option value="affa3">AFFA3</option>
        <option value="affa3nav">AFFA3Nav</option>
    </select>
    <input type="submit" value="Save" />
    </form>

  <h2>Auto-restore text</h2>
    <form action="/config/restore" method="GET">
    <label>    
    
        <input type="checkbox" name="enable"  id="autoRestoreCheckbox"  value="1"  />
        Auto-Restore texts on startup
    </label>
        <!-- Hidden input to force sending value when checkbox is unchecked -->
        <input type="hidden" name="enable" value="0" />
    <input type="submit" value="Save" />
    </form>

  <h2>Show Static Text</h2>
  <form action="/static" method="GET">
    <label>Text (max 8 chars):</label>
    <input type="text" name="text" maxlength="8" id="staticTextInput" required />
    <label><input type="checkbox" name="save" /> Save</label><br><br>
    <input type="submit" value="Show Static Text" />
  </form>

  <hr>

  <h2>Scroll Welcome Text</h2>
  <form action="/scroll" method="GET">
    <label>Text:</label>
    <input type="text" id="welcomeTextInput" name="text" required />
    <label><input type="checkbox" name="save" /> Save as Welcome</label><br><br>
    <input type="submit" value="Scroll Text" />
  </form>

  <hr>

    <h2>Set Time</h2>
    <form action="/settime" method="GET">
    <label>Time (HHMM, e.g. 0930):</label>
    <input type="text" name="time" pattern="\d{4}" maxlength="4" required />
    <input type="submit" value="Set Time" />
    </form>
 
  <hr>

    <h2>Set Time</h2>
    <form action="/setVoltage" method="GET">
    <label>Voltage :</label>
    <input type="text" name="voltage" required />
    <input type="submit" value="Set Voltage" />
    </form>
    <hr> 
 
    <hr>
    <h2>OTA Update</h2>
    <p>Use the <a href="/update">OTA Update</a> link to upload new firmware.</p>
 
    <hr>
    <h2>Button emulation</h2>
    <p>Use the <a href="/emulate">Button emulation</a> link to debug menu.</p>
</body>
</html>
)rawliteral";

void HttpServerManager::begin()
{
    _server.listen(80);

    ElegantOTA.begin(&_server);
    setupRoutes();
    Serial.println("HTTP Server: routes initialized.");
}

void HttpServerManager::setupRoutes()
{

    _server.on("/", HTTP_GET, [this](PsychicRequest *request)
               { return request->reply(200, "text/html", htmlPage); });

    _server.on("/static", HTTP_GET, [this](PsychicRequest *request)
               {
        if (!request->hasParam("text")) {
            return request->reply(400, "text/plain", "Missing 'text' parameter");
        }
        String text = request->getParam("text")->value();
        bool save = request->hasParam("save");

        // Call your command handler
        _commands.showText(text, save);
        String response = "Static Text shown: " + text;
        return request->reply(200, "text/plain", response.c_str()); });

    _server.on("/scroll", HTTP_GET, [this](PsychicRequest *request)
               {    
        if (!request->hasParam("text")) {
            return request->reply(400, "text/plain", "Missing 'text' parameter");   
        }
        String text = request->getParam("text")->value();   
        bool save = request->hasParam("save");

        // Call your command handler
        if (save) {
            // Scroll AND save as welcome text
            _commands.setWelcomeText(text);
        } else {
            // Scroll temporarily only
            _commands.scrollText(text);
        } 
        String response = save ? "Text scrolled and saved as welcome: " + text
                            : "Text scrolled temporarily: " + text;

        return request->reply(200, "text/plain", response.c_str()); });

    _server.on("/config/restore", HTTP_GET, [this](PsychicRequest *request)
               {
        if (request->hasParam("enable")) {
            bool enable = request->getParam("enable")->value() == "1";
            _prefs.begin("display", false);
            _prefs.putBool("autoRestore", enable);
            _prefs.end();
            String resp = enable ? "Auto restore enabled" : "Auto restore disabled";
            return request->reply(200, "text/plain", resp.c_str());
        } else {
            _prefs.begin("display", true);
            bool autoRestore = _prefs.getBool("autoRestore", false);
            _prefs.end();
            String resp = autoRestore ? "1" : "0";
            return request->reply(200, "text/plain", resp.c_str());
        } });

    _server.on("/getlasttext", HTTP_GET, [this](PsychicRequest *request)
               {
        _prefs.begin("display", true);
        String lastText = _prefs.getString("lastText", "");
        _prefs.end();
        return request->reply(200, "text/plain", lastText.c_str()); });

    _server.on("/getwelcometext", HTTP_GET, [this](PsychicRequest *request)
               {
        _prefs.begin("display", true);
        String welcomeText = _prefs.getString("welcomeText", "");
        _prefs.end();
        return request->reply(200, "text/plain", welcomeText.c_str()); });
        
    _server.on("/settime", HTTP_GET, [this](PsychicRequest *request)
               {
        if (!request->hasParam("time")) {
            return request->reply(400, "text/plain", "Missing 'time' parameter");
        }
        String timeStr = request->getParam("time")->value();

        if (timeStr.length() != 4) {
            return request->reply(400, "text/plain", "Invalid time format. Use 4 digits like '1234'");
        }

        _commands.setTime(timeStr);
        String response = "Time set to: " + timeStr;
        return request->reply(200, "text/plain", response.c_str()); });

    _server.on("/setVoltage", HTTP_GET, [this](PsychicRequest *request)
               {
        if (!request->hasParam("voltage")) {
            return request->reply(400, "text/plain", "Missing 'voltage' parameter");
        }
        String str = request->getParam("voltage")->value();

        if (str.length() <= 0 ) {
            return request->reply(400, "text/plain", "Invalid voltage format. input is empty");
        }

        _commands.setVoltage(str.toInt());
        String response = "voltage set to: " + str;
        return request->reply(200, "text/plain", response.c_str()); });

    _server.on("/setDisplay", HTTP_POST, [](PsychicRequest *request) {
    if (request->hasParam("type")) 
    {
        String type = request->getParam("type")->value();

        Preferences prefs;
        prefs.begin("config", false);
        prefs.putString("display_type", type);
        prefs.end();
        delay(1000); // Give time for ESP to restart
        ESP.restart(); // Optional: force restart to apply change
        return request->reply(200, "text/plain", "Display type saved. Restart required.");
    }
});
// // pseudo (adapt to your web lib)
// server.on("/api/live", HTTP_GET, [this] (auto* req) {
//   if (!elmManager) { req->send(503, "application/json", "{}"); return; }
//   req->send(200, "application/json", elmManager->snapshotJson());
// });

_server.on("/api/live", HTTP_GET, [this](PsychicRequest *req){
    if (!elm) { return req->reply(503, "application/json", "{\"error\":\"elm not ready\"}"); }
    return req->reply(200, "application/json", elm->snapshotJson().c_str());
  });



_server.on("/affa3/setMenu", HTTP_GET, [this](PsychicRequest *request)
{
    if (!request->hasParam("caption") || 
        !request->hasParam("name1") || 
        !request->hasParam("name2")) 
    {
        return request->reply(400, "text/plain", "Missing one or more required parameters: caption, name1, name2");
    }

    String caption = request->getParam("caption")->value();
    String name1 = request->getParam("name1")->value();
    String name2 = request->getParam("name2")->value();

     uint8_t scrollLockIndicator = 0x0B; // default value

    if (request->hasParam("scrollLock")) {
        String scrollLockStr = request->getParam("scrollLock")->value();
        if (scrollLockStr.length() != 2 ) {
            return request->reply(400, "text/plain", "Invalid scrollLock format. Use two-digit hex like '7E'");
        }
        scrollLockIndicator = (uint8_t) strtoul(scrollLockStr.c_str(), nullptr, 16);
    }

    Serial.printf("[showMenu] caption='%s' name1='%s' name2='%s' scrollLock=0x%02X\n",
                  caption.c_str(), name1.c_str(), name2.c_str(), scrollLockIndicator);

    _commands.showMenu(caption.c_str(), name1.c_str(), name2.c_str(), scrollLockIndicator);

    String response = "Menu sent with scrollLock=0x" + String(scrollLockIndicator, HEX);
    return request->reply(200, "text/plain", response.c_str());
});

        
    // Serve a dedicated page for Affa3 display test commands
    _server.on("/affa3test", HTTP_GET, [this](PsychicRequest *request)
               {
    const char *page = R"rawliteral(
        <!DOCTYPE html>
        <html><head><title>Affa3 Display Test</title></head><body>
        <h2>Set Menu</h2>
        <form action="/affa3/setMenu" method="GET">
            Caption: <input name="caption" required><br>
            Name1: <input name="name1" required><br>
            Name2: <input name="name2" required><br> 
            Scroll Lock (Hex): <input name="scrollLock" value="0B" pattern="[0-9a-fA-F]{2}" ><br>
  
            <input type="submit" value="Set Menu">
        </form>

        <h2>Set AUX</h2>
        <form action="/affa3/setAux" method="GET">
            <input type="submit" value="Set AUX">
        </form>

        <h2>Set Big Text</h2>
        <form action="/affa3/setTextBig" method="GET">
            Caption: <input name="caption" required><br>
            Row1: <input name="row1" required><br>
            Row2: <input name="row2" required><br>
            <input type="submit" value="Set Big Text">
        </form>
        </body></html>
    )rawliteral";
    return request->reply(200, "text/html", page); });

    _server.on("/emulate", HTTP_GET, [this](PsychicRequest *request){

        const char * page = R"rawliteral(
        <!DOCTYPE html>
        <html><head><title>Affa3 Display Button Test</title></head><body>
        <h2>Emulate Buttons</h2>

        <form onsubmit="return false">
        <button onclick="sendKey(0x0000, 0)">Load</button>
        <button onclick="sendKey(0x0000, 1)">Load (Hold)</button><br>

        <button onclick="sendKey(0x0001, 0)">SrcRight</button>
        <button onclick="sendKey(0x0001, 1)">SrcRight (Hold)</button><br>

        <button onclick="sendKey(0x0002, 0)">SrcLeft</button>
        <button onclick="sendKey(0x0002, 1)">SrcLeft (Hold)</button><br>

        <button onclick="sendKey(0x0003, 0)">Vol+</button>
        <button onclick="sendKey(0x0003, 1)">Vol+ (Hold)</button><br>

        <button onclick="sendKey(0x0004, 0)">Vol-</button>
        <button onclick="sendKey(0x0004, 1)">Vol- (Hold)</button><br>

        <button onclick="sendKey(0x0005, 0)">Pause</button>
        <button onclick="sendKey(0x0005, 1)">Pause (Hold)</button><br>

        <button onclick="sendKey(0x0101, 0)">RollUp</button>
        <button onclick="sendKey(0x0101, 1)">RollUp (Hold)</button><br>

        <button onclick="sendKey(0x0141, 0)">RollDown</button>
        <button onclick="sendKey(0x0141, 1)">RollDown (Hold)</button><br>
        </form>

        <script>
        async function sendKey(key, hold) {
            const formData = new URLSearchParams();
            formData.append("key", key);
            formData.append("hold", hold ? "1" : "0");

            const res = await fetch("/emulate/key", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: formData
            });

            const txt = await res.text();
            console.log("Response:", txt);
        }
        </script>
                </body></html>
    )rawliteral";

        return request->reply(200, "text/html", page);
    });

    _server.on("/emulate/key", HTTP_POST, [this](PsychicRequest *request) {
        if (!request->hasParam("key") || !request->hasParam("hold")) {
            return request->reply(400, "text/plain", "Missing key or hold");
        }

        uint16_t keyCode = request->getParam("key")->value().toInt();
        bool isHold = request->getParam("hold")->value() == "1";

        AffaCommon::AffaKey key = static_cast<AffaCommon::AffaKey>(keyCode);
        _commands.OnKeyPressed(key, isHold); 

        String msg = String("Emulated key 0x") + String(keyCode, HEX) + (isHold ? " (Hold)" : " (Press)");
        return request->reply(200, "text/plain", msg.c_str());
    });
 
}
