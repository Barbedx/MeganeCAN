#include "HttpServerManager.h"
#include "effects/ScrollEffect.h"

HttpServerManager::HttpServerManager(PsychicHttpServer& server, Preferences& prefs, Affa3NavDisplay& display)
    : _server(server), _prefs(prefs), _display(display) {}

void HttpServerManager::begin() {
    _server.listen(80);
    setupRoutes();
    Serial.println("HTTP Server: routes initialized.");
}

void HttpServerManager::setupRoutes() {
    _server.on("/help", HTTP_GET, [this](PsychicRequest *request) {
        String helpText = 
        "Available Commands:\n\n"
        "/settext?text=<your_text>\n"
        "  - Displays the given text on the screen and store it to memory.\n"
        "  - Example: /showtext?text=HelloWorld\n\n";
        "/scrolltext?text=<your_text>\n"
        "  - Displays the given text on the screen scrolled.\n"
        "  - Example: /scrolltext?text=HelloWorld\n\n";
        "/setwelcometext?text=<your_text>\n"
        "  - set welcominmg message scrolled.\n"
        "  - Example: /scrolltext?text=HelloWorld\n\n";
        "/gettext\n"
        "  - get base message text.\n"
        "  - Example: /scrolltext?text=HelloWorld\n\n";
        return request->reply(200, "text/plain", helpText.c_str());
    });

    _server.on("/settext", HTTP_GET, [this](PsychicRequest *request) {
        if (request->hasParam("text")) {
            String text = request->getParam("text")->value();
            _display.setState(true);
            _display.setText(text.c_str());

            _prefs.begin("display", false);
            _prefs.putString("lastText", text);
            _prefs.end();

        String response = "Text shown: " + text;
            return request->reply(200, "text/plain", response.c_str() );
        } else {
            return request->reply(400, "text/plain", "Missing 'text' parameter");
        }
    });

    // _server.on("/setwelcometext", HTTP_GET, [this](PsychicRequest *request) {
    //     if (request->hasParam("text")) {
    //         String text = request->getParam("text")->value();

    //         _prefs.begin("display", false);
    //         _prefs.putString("welcomeText", text);
    //         _prefs.end();

    //         _display.setState(true);
    //         ScrollEffect(&_display, ScrollDirection::Left, text.c_str(), 250);

    //         return request->reply(200, "text/plain", "Text saved for welcoming: " + text);
    //     } else {
    //         return request->reply(400, "text/plain", "Missing 'text' parameter");
    //     }
    // });

    // _server.on("/scrolltext", HTTP_GET, [this](PsychicRequest *request) {
    //     if (request->hasParam("text")) {
    //         String text = request->getParam("text")->value();
    //         _display.setState(true);
    //         ScrollEffect(&_display, ScrollDirection::Left, text.c_str(), 250);

    //         return request->reply(200, "text/plain", "Text shown: " + text);
    //     } else {
    //         return request->reply(400, "text/plain", "Missing 'text' parameter");
    //     }
    // });

    // _server.on("/gettext", HTTP_GET, [this](PsychicRequest *request) {
    //     _prefs.begin("display", true);
    //     String savedText = _prefs.getString("lastText", "");
    //     _prefs.end();

    //     if (savedText.length() > 0) {
    //         return request->reply(200, "text/plain", "Text shown: " + savedText);
    //     } else {
    //         return request->reply(404, "text/plain", "No text stored");
    //     }
    // });
}
