#include "HttpServerManager.h"
#include "effects/ScrollEffect.h"
#include "../commands/DisplayCommands.h"
#include "../bluetooth.h"
#include "../wifi_manager.h"
#include "../apple_media_service.h"
#include "../apple_notification_service.h"
#include "../utils/Log.h"
#include "../utils/CanLog.h"
#include "../utils/AppConfig.h"
#include "../wire/WsWireLink.h"
#include "WirePage.h"
#include "DashboardPage.h"
#include "DiagPage.h"
#include "Affa3TestPage.h"
#include <ElegantOTA.h>

HttpServerManager::HttpServerManager(IDisplay &display, Preferences &prefs) : _server(),
                                                                              _display(display),
                                                                              _prefs(prefs),
                                                                              _commands(display, prefs)
{
}

namespace
{
    std::string jsonEsc(const std::string &in)
    {
        std::string out;
        for (unsigned char c : in)
        {
            if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
            else if (c == '\n') out += ' ';
            else if (c >= 0x20) out += (char)c;
        }
        return out;
    }

    const char *pbState(AppleMediaService::MediaInformation::PlaybackState s)
    {
        using St = AppleMediaService::MediaInformation::PlaybackState;
        switch (s) { case St::Playing: return "Playing"; case St::Paused: return "Paused";
                     case St::Rewinding: return "Rewinding"; case St::FastForwarding: return "FastForwarding";
                     default: return "Unknown"; }
    }

    String buildMediaJson()
    {
        bool conn = Bluetooth::IsConnected();
        const auto &m = AppleMediaService::GetMediaInformation();
        float elapsed = m.mElapsedTime;
        if (conn && m.mPlaybackState == AppleMediaService::MediaInformation::PlaybackState::Playing && m.mLastPlaybackInfoMs)
            elapsed += (millis() - m.mLastPlaybackInfoMs) / 1000.0f * (m.mPlaybackRate > 0 ? m.mPlaybackRate : 1.0f);
        String j = "{\"connected\":";
        j += conn ? "true" : "false";
        if (conn)
        {
            j += ",\"player\":\"" + String(jsonEsc(m.mPlayerName).c_str()) + "\"";
            j += ",\"title\":\""  + String(jsonEsc(m.mTitle).c_str())  + "\"";
            j += ",\"artist\":\"" + String(jsonEsc(m.mArtist).c_str()) + "\"";
            j += ",\"album\":\""  + String(jsonEsc(m.mAlbum).c_str())  + "\"";
            j += ",\"state\":\""  + String(pbState(m.mPlaybackState))  + "\"";
            j += ",\"elapsed\":"  + String(elapsed, 1);
            j += ",\"duration\":" + String(m.mDuration, 1);
            j += ",\"volume\":"   + String(m.mVolume, 2);
            j += ",\"queueIndex\":" + String(m.mQueueIndex);
            j += ",\"queueCount\":" + String(m.mQueueCount);
            j += ",\"shuffle\":"  + String((int)m.mShuffleMode);
            j += ",\"repeat\":"   + String((int)m.mRepeatMode);
        }
        j += "}";
        return j;
    }

    String buildNotifsJson()
    {
        auto recents = AppleNotificationService::GetRecent();
        String j = "[";
        for (size_t i = 0; i < recents.size(); i++)
        {
            const auto &n = recents[i];
            if (i) j += ",";
            j += "{\"cat\":\""  + String(AppleNotificationService::CategoryName(n.categoryId)) + "\"";
            j += ",\"app\":\""   + String(jsonEsc(n.appId).c_str())   + "\"";
            j += ",\"title\":\"" + String(jsonEsc(n.title).c_str())   + "\"";
            j += ",\"msg\":\""   + String(jsonEsc(n.message).c_str()) + "\"}";
        }
        j += "]";
        return j;
    }
} // namespace


void HttpServerManager::begin()
{
    // 46 app routes + ElegantOTA's handlers exceed 48 -> the tail routes failed
    // to register (ESP_ERR_HTTPD_HANDLERS_FULL). Bump the cap so every route fits.
    _server.config.max_uri_handlers = 64;
    // The dashboard opens several parallel keep-alive sockets; on a memory-tight
    // ESP32 they pin ~4KB each and the server eventually can't accept new ones
    // (dashboard wedges, heap stuck). LRU purge lets it drop the oldest idle
    // socket to serve a new request, and a smaller socket cap bounds the RAM the
    // connections hold. Slower (some requests queue) but stable.
    _server.config.lru_purge_enable = true;
    // One extra socket for the WebSocket frame stream (/canstream) on top of the
    // dashboard's keep-alive sockets. Watch [heap] — each socket pins ~4KB.
    _server.config.max_open_sockets = 5;
    _server.listen(80);

    ElegantOTA.begin(&_server);
    setupRoutes();
    if (_wire)
        _wire->attach(_server, "/canstream");   // WebSocket WireProto stream
    Serial.println("HTTP Server: routes initialized.");
}

void HttpServerManager::setupRoutes()
{
    _server.on("/", HTTP_GET, [this](PsychicRequest *request)
               { return request->reply(200, "text/html", DASHBOARD_PAGE); });

    // Wireless CAN viewer + display steering (WebSocket /canstream client). Open
    // http://<esp-ip>/wire from any phone/PC on the ESP's network.
    _server.on("/wire", HTTP_GET, [this](PsychicRequest *request)
               { return request->reply(200, "text/html", WIRE_PAGE); });

    _server.on("/static", HTTP_GET, [this](PsychicRequest *request) {
        if (!request->hasParam("text"))
            return request->reply(400, "text/plain", "Missing 'text' parameter");
        String text = request->getParam("text")->value();
        bool save = request->hasParam("save");
        _commands.showText(text, save);
        String msg = "Static Text shown: " + text;
        return request->reply(200, "text/plain", msg.c_str());
    });

    _server.on("/scroll", HTTP_GET, [this](PsychicRequest *request) {
        if (!request->hasParam("text"))
            return request->reply(400, "text/plain", "Missing 'text' parameter");
        String text = request->getParam("text")->value();
        bool save = request->hasParam("save");
        if (save)
            _commands.setWelcomeText(text);
        else
            _commands.scrollText(text);
        String msg = save ? "Text scrolled and saved as welcome: " + text
                          : "Text scrolled temporarily: " + text;
        return request->reply(200, "text/plain", msg.c_str());
    });

    _server.on("/config/restore", HTTP_GET, [this](PsychicRequest *request) {
        if (request->hasParam("enable")) {
            bool enable = request->getParam("enable")->value() == "1";
            _prefs.begin("display", false);
            _prefs.putBool("autoRestore", enable);
            _prefs.end();
            return request->reply(200, "text/plain", enable ? "Auto restore enabled" : "Auto restore disabled");
        } else {
            _prefs.begin("display", true);
            bool autoRestore = _prefs.getBool("autoRestore", false);
            _prefs.end();
            return request->reply(200, "text/plain", autoRestore ? "1" : "0");
        }
    });

    _server.on("/getlasttext", HTTP_GET, [this](PsychicRequest *request) {
        _prefs.begin("display", true);
        String lastText = _prefs.getString("lastText", "");
        _prefs.end();
        return request->reply(200, "text/plain", lastText.c_str());
    });

    _server.on("/getwelcometext", HTTP_GET, [this](PsychicRequest *request) {
        _prefs.begin("display", true);
        String welcomeText = _prefs.getString("welcomeText", "");
        _prefs.end();
        return request->reply(200, "text/plain", welcomeText.c_str());
    });

    _server.on("/settime", HTTP_GET, [this](PsychicRequest *request) {
        if (!request->hasParam("time"))
            return request->reply(400, "text/plain", "Missing 'time' parameter");
        String timeStr = request->getParam("time")->value();
        if (timeStr.length() != 4)
            return request->reply(400, "text/plain", "Invalid time format. Use 4 digits like '1234'");
        _commands.setTime(timeStr);
        String msg = "Time set to: " + timeStr;
        return request->reply(200, "text/plain", msg.c_str());
    });

    _server.on("/setVoltage", HTTP_GET, [this](PsychicRequest *request) {
        if (!request->hasParam("voltage"))
            return request->reply(400, "text/plain", "Missing 'voltage' parameter");
        String str = request->getParam("voltage")->value();
        if (str.length() == 0)
            return request->reply(400, "text/plain", "Invalid voltage format. input is empty");
        _commands.setVoltage(str.toInt());
        String msg = "voltage set to: " + str;
        return request->reply(200, "text/plain", msg.c_str());
    });

    _server.on("/getdisplaytype", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "text/plain", AppConfig::displayType.c_str());
    });

    _server.on("/getbtmode", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "text/plain", AppConfig::btMode.c_str());
    });

    _server.on("/getautotime", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "text/plain", AppConfig::autoTime ? "1" : "0");
    });

    _server.on("/getskipfuncreg", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "text/plain", AppConfig::skipFuncReg ? "1" : "0");
    });

    _server.on("/setskipfuncreg", HTTP_POST, [](PsychicRequest *request) {
        bool skip = request->hasParam("skip_funcreg") &&
                    request->getParam("skip_funcreg")->value() == "1";
        Preferences prefs;
        prefs.begin("config", false);
        prefs.putBool("skip_funcreg", skip);
        prefs.end();
        ESP.restart();
        return request->reply(200, "text/plain", skip ? "Func-reg skip enabled. Restarting..." : "Func-reg skip disabled. Restarting...");
    });

    _server.on("/setDisplay", HTTP_POST, [](PsychicRequest *request) {
        if (!request->hasParam("type")) {
            return request->reply(400, "text/plain", "Missing 'type' parameter");
        }
        String type = request->getParam("type")->value();
        Preferences prefs;
        prefs.begin("config", false);
        prefs.putString("display_type", type);
        prefs.end();
        ESP.restart();
        return request->reply(200, "text/plain", "Display type saved. Restarting...");
    });

    _server.on("/setbtmode", HTTP_POST, [](PsychicRequest *request) {
        if (!request->hasParam("mode")) {
            return request->reply(400, "text/plain", "Missing 'mode' parameter");
        }
        String mode = request->getParam("mode")->value();
        bool autoTime = request->hasParam("auto_time") &&
                        request->getParam("auto_time")->value() == "1";
        Preferences prefs;
        prefs.begin("config", false);
        prefs.putString("bt_mode", mode);
        prefs.putBool("auto_time", autoTime);
        prefs.end();
        ESP.restart();
        return request->reply(200, "text/plain", "BT mode saved. Restarting...");
    });

    _server.on("/getelmenabled", HTTP_GET, [](PsychicRequest *request) {
        Preferences prefs;
        prefs.begin("config", true);
        bool en = prefs.getBool("elm_enabled", false);
        prefs.end();
        return request->reply(200, "text/plain", en ? "1" : "0");
    });

    _server.on("/setelm", HTTP_POST, [](PsychicRequest *request) {
        bool enable = request->hasParam("elm_enabled") &&
                      request->getParam("elm_enabled")->value() == "1";
        Preferences prefs;
        prefs.begin("config", false);
        prefs.putBool("elm_enabled", enable);
        prefs.end();
        ESP.restart();
        return request->reply(200, "text/plain", enable ? "ELM enabled. Restarting..." : "ELM disabled. Restarting...");
    });

    _server.on("/clearbonds", HTTP_POST, [](PsychicRequest *request) {
        Bluetooth::ClearBonds();
        return request->reply(200, "text/plain", "BLE bonds cleared. Re-pair on iPhone.");
    });

    _server.on("/forgetdevice", HTTP_POST, [](PsychicRequest *request) {
        Bluetooth::ClearBonds(); // peripheral model: forget = clear bonds + re-advertise
        return request->reply(200, "text/plain", "Device forgotten. Re-pair from iOS Settings.");
    });

    // --- WiFi (home network) configuration ---
    _server.on("/api/wifi", HTTP_GET, [](PsychicRequest *request) {
        String j = "{";
        j += "\"mode\":\"" + String(WiFiManager::ModeStr().c_str()) + "\",";
        j += "\"ssid\":\"" + String(WiFiManager::SSID().c_str()) + "\",";
        j += "\"ip\":\"" + String(WiFiManager::IP().c_str()) + "\",";
        j += "\"host\":\"" + String(WiFiManager::Hostname().c_str()) + "\"}";
        return request->reply(200, "application/json", j.c_str());
    });

    _server.on("/api/wifi/scan", HTTP_GET, [](PsychicRequest *request) {
        WiFiManager::StartScan();
        return request->reply(200, "text/plain", "ok");
    });

    _server.on("/api/wifi/networks", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "application/json", WiFiManager::ScanJson().c_str());
    });

    _server.on("/api/wifi/save", HTTP_POST, [](PsychicRequest *request) {
        if (!request->hasParam("ssid"))
            return request->reply(400, "text/plain", "missing ssid");
        std::string ssid = request->getParam("ssid")->value().c_str();
        std::string pass = request->hasParam("pass") ? std::string(request->getParam("pass")->value().c_str()) : "";
        std::string ip   = request->hasParam("ip")   ? std::string(request->getParam("ip")->value().c_str())   : "";
        WiFiManager::SaveCredentials(ssid, pass, ip);
        esp_err_t e = request->reply(200, "text/plain", "Saved. Rebooting to join the network...");
        delay(400);
        ESP.restart();
        return e;
    });

    _server.on("/api/live", HTTP_GET, [this](PsychicRequest *request) {
        if (!elm) return request->reply(503, "application/json", "{\"error\":\"elm not ready\"}");
        return request->reply(200, "application/json", elm->snapshotJson().c_str());
    });

    _server.on("/api/live/full", HTTP_GET, [this](PsychicRequest *request) {
        if (!elm) return request->reply(503, "application/json", "[]");
        return request->reply(200, "application/json", elm->fullSnapshotJson().c_str());
    });

    _server.on("/api/elm/headers", HTTP_GET, [this](PsychicRequest *request) {
        if (!elm) return request->reply(503, "application/json", "{}");
        return request->reply(200, "application/json", elm->headersJson().c_str());
    });

    _server.on("/api/elm/plan", HTTP_GET, [this](PsychicRequest *request) {
        if (!elm) return request->reply(503, "application/json", "{}");
        return request->reply(200, "application/json", elm->planJson().c_str());
    });

    _server.on("/api/elm/scan", HTTP_POST, [this](PsychicRequest *request) {
        if (!elm)
            return request->reply(503, "application/json", "{\"error\":\"elm not available\"}");
        if (!request->hasParam("header") || !request->hasParam("pid"))
            return request->reply(400, "application/json", "{\"error\":\"missing header or pid\"}");
        if (elm->isScanBusy())
            return request->reply(409, "application/json", "{\"error\":\"scan already in progress\"}");

        String header = request->getParam("header")->value();
        String pid    = request->getParam("pid")->value();

        if (!elm->requestScan(header.c_str(), pid.c_str()))
            return request->reply(409, "application/json", "{\"error\":\"scan already in progress\"}");

        // Block this HTTP task until tick() signals done (up to 5 s)
        if (!elm->waitScan(5000)) {
            elm->cancelScan();
            return request->reply(408, "application/json", "{\"error\":\"scan timeout\"}");
        }

        const ScanResult& r = elm->lastScanResult();
        if (!r.errorMsg.isEmpty())
            return request->reply(500, "application/json",
                                  ("{\"error\":\"" + r.errorMsg + "\"}").c_str());

        // Build hex string
        String hexStr;
        for (uint8_t b : r.bytes) {
            if (hexStr.length()) hexStr += " ";
            char h[3]; snprintf(h, sizeof(h), "%02X", b);
            hexStr += h;
        }
        String json = "{\"header\":\"" + header + "\",\"pid\":\"" + pid +
                      "\",\"raw\":\"" + hexStr + "\",\"metrics\":" + r.metricsJson + "}";
        return request->reply(200, "application/json", json.c_str());
    });

    _server.on("/diag", HTTP_GET, [](PsychicRequest *request) {
        const char *page = DIAG_PAGE;
        return request->reply(200, "text/html", page);
    });

    _server.on("/api/elm/headers", HTTP_POST, [this](PsychicRequest *request) {
        if (!elm) return request->reply(503, "application/json", "{}");
        if (!request->hasParam("header") || !request->hasParam("enabled"))
            return request->reply(400, "text/plain", "Missing header or enabled");
        String hdr = request->getParam("header")->value();
        bool en = request->getParam("enabled")->value() != "0";
        elm->setHeaderEnabled(hdr.c_str(), en);
        elm->saveHeaderConfig(_prefs);
        return request->reply(200, "text/plain", "OK");
    });

    _server.on("/affa3/setMenu", HTTP_GET, [this](PsychicRequest *request) {
        if (!request->hasParam("caption") || !request->hasParam("name1") || !request->hasParam("name2"))
            return request->reply(400, "text/plain", "Missing one or more required parameters: caption, name1, name2");
        String caption = request->getParam("caption")->value();
        String name1   = request->getParam("name1")->value();
        String name2   = request->getParam("name2")->value();
        uint8_t scrollLockIndicator = 0x0B;
        if (request->hasParam("scrollLock")) {
            String scrollLockStr = request->getParam("scrollLock")->value();
            if (scrollLockStr.length() != 2)
                return request->reply(400, "text/plain", "Invalid scrollLock format. Use two-digit hex like '7E'");
            scrollLockIndicator = (uint8_t) strtoul(scrollLockStr.c_str(), nullptr, 16);
        }
        Serial.printf("[showMenu] caption='%s' name1='%s' name2='%s' scrollLock=0x%02X\n",
                      caption.c_str(), name1.c_str(), name2.c_str(), scrollLockIndicator);
        _commands.showMenu(caption.c_str(), name1.c_str(), name2.c_str(), scrollLockIndicator);
        String msg = "Menu sent with scrollLock=0x" + String(scrollLockIndicator, HEX);
        return request->reply(200, "text/plain", msg.c_str());
    });

    _server.on("/affa3test", HTTP_GET, [](PsychicRequest *request) {
        const char *page = AFFA3TEST_PAGE;
        return request->reply(200, "text/html", page);
    });


    _server.on("/setaux", HTTP_POST, [this](PsychicRequest *request) {
        _display.setAuxMode(true);
        return request->reply(200, "text/plain", "AUX mode activated");
    });

    _server.on("/display/state", HTTP_POST, [this](PsychicRequest *request) {
        bool enable = request->hasParam("enable") &&
                      request->getParam("enable")->value() == "1";
        Serial.printf("[HTTP /display/state] enable=%d\n", enable);
        _display.setState(enable);
        return request->reply(200, "text/plain", enable ? "Display ON" : "Display OFF");
    });

    _server.on("/restart", HTTP_POST, [](PsychicRequest *request) {
        request->reply(200, "text/plain", "Restarting...");
        delay(200);
        ESP.restart();
        return ESP_OK;
    });

    _server.on("/api/bt", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "application/json", Bluetooth::GetStatusJson().c_str());
    });

    _server.on("/api/media", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "application/json", buildMediaJson().c_str());
    });

    // Consolidated dashboard poll: one request returns media + notifs + bt + wifi
    // + can, so the page polls a single keep-alive socket instead of 5 in parallel
    // (fewer connection buffers, less heap churn on a RAM-tight ESP32). Reuses the
    // same builders as the individual /api/* routes.
    // Bench emulator self-ACK toggle (reliable HTTP path; the serial @EMU exists too).
    // /api/emu?on=1 makes multi-frame display sends emit their full AFFA3 sequence
    // without a real display, for the PC-side CAN emulator to decode.
    _server.on("/api/emu", HTTP_GET, [this](PsychicRequest *request) {
        bool on = !request->hasParam("on") || request->getParam("on")->value() != "0";
        _display.setEmuSelfAck(on);
        return request->reply(200, "text/plain", on ? "emu self-ack ON" : "emu self-ack OFF");
    });

    // FULL-EMULATION: an in-firmware virtual display ACKs the radio so it emits the
    // whole AFFA3 sequence with no real panel + no self-ACK, and the ESP decodes its
    // own screen. setFullEmu/fullEmuScreenJson live in main.cpp (own the virtual disp).
    _server.on("/api/fullemu", HTTP_GET, [](PsychicRequest *request) {
        extern void setFullEmu(bool);
        bool on = !request->hasParam("on") || request->getParam("on")->value() != "0";
        setFullEmu(on);
        return request->reply(200, "text/plain", on ? "full-emulation ON" : "full-emulation OFF");
    });

    // The ESP's own decoded screen (meaningful while full-emulation is on).
    _server.on("/api/screen", HTTP_GET, [](PsychicRequest *request) {
        extern String fullEmuScreenJson();
        return request->reply(200, "application/json", fullEmuScreenJson().c_str());
    });

    // Memory + liveness in one place — the single endpoint to "stably understand how
    // much it eats". free/min/maxblk are the heap; maxblk (largest contiguous block)
    // is the number that actually gates BLE+WiFi+HTTP allocations on this device.
    // Hand-built into a fixed buffer (no String churn). Poll it; watch maxblk trend.
    _server.on("/api/health", HTTP_GET, [this](PsychicRequest *request) {
        char j[360];
        snprintf(j, sizeof(j),
            "{\"heap\":{\"free\":%u,\"min\":%u,\"maxblk\":%u,\"total\":%u},"
            "\"uptime_ms\":%lu,\"ws\":{\"clients\":%d,\"dropped\":%lu},"
            "\"cfg\":{\"provisioned\":%s,\"schema\":%lu,\"display\":\"%s\"}}",
            (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
            (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getHeapSize(),
            (unsigned long)millis(),
            _wire ? _wire->clientCount() : 0,
            (unsigned long)(_wire ? _wire->dropped() : 0),
            AppConfig::provisioned ? "true" : "false",
            (unsigned long)AppConfig::schemaVersion,
            AppConfig::displayKindStr(AppConfig::displayKind()));
        return request->reply(200, "application/json", j);
    });

    // First-run provisioning: one atomic call sets the two settings a fleet device
    // must pick (display protocol + BT mode), marks it provisioned, and restarts.
    // Frontends check /api/health cfg.provisioned and show a setup step when false.
    _server.on("/api/setup", HTTP_POST, [this](PsychicRequest *request) {
        if (!request->hasParam("displayType") || !request->hasParam("btMode"))
            return request->reply(400, "text/plain", "need displayType + btMode");
        String dt = request->getParam("displayType")->value();
        String bm = request->getParam("btMode")->value();
        if (dt != "carminat" && dt != "updatelist" && dt != "updatelist_menu")
            return request->reply(400, "text/plain", "bad displayType");
        if (bm != "ams" && bm != "keyboard")
            return request->reply(400, "text/plain", "bad btMode");
        _prefs.begin("config", false);
        _prefs.putString("display_type", dt);
        _prefs.putString("bt_mode", bm);
        _prefs.putBool("provisioned", true);
        _prefs.end();
        request->reply(200, "text/plain", "provisioned, restarting");
        delay(200);
        ESP.restart();
        return ESP_OK;
    });

    // Info popup — through the IDisplay abstraction only (the server knows nothing
    // about CarminatDisplay). 3 short lines; displays that don't support it no-op.
    _server.on("/api/info", HTTP_GET, [this](PsychicRequest *request) {
        auto p = [&](const char *k) {
            return request->hasParam(k) ? request->getParam(k)->value() : String("");
        };
        _display.showInfoPopup(p("l1").c_str(), p("l2").c_str(), p("l3").c_str());
        return request->reply(200, "text/plain", "info popup sent");
    });

    // Best-effort close: hide the popup (back to normal screen).
    _server.on("/api/info/close", HTTP_GET, [this](PsychicRequest *request) {
        _display.hideInfoPopup();
        return request->reply(200, "text/plain", "info popup close sent");
    });

    _server.on("/api/dashboard", HTTP_GET, [](PsychicRequest *request) {
        String j = "{\"media\":";
        j += buildMediaJson();
        j += ",\"notifs\":";
        j += buildNotifsJson();
        j += ",\"bt\":";
        j += Bluetooth::GetStatusJson();
        j += ",\"wifi\":{\"mode\":\"" + String(WiFiManager::ModeStr().c_str()) +
             "\",\"ssid\":\"" + String(WiFiManager::SSID().c_str()) +
             "\",\"ip\":\"" + String(WiFiManager::IP().c_str()) +
             "\",\"host\":\"" + String(WiFiManager::Hostname().c_str()) + "\"}";
        j += ",\"can\":";
        j += CanLog::configJson().c_str();
        j += "}";
        return request->reply(200, "application/json", j.c_str());
    });

    _server.on("/api/notifs", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "application/json", buildNotifsJson().c_str());
    });

    _server.on("/api/cmd", HTTP_GET, [](PsychicRequest *request) {
        using ID = AppleMediaService::RemoteCommandID;
        if (!request->hasParam("c"))
            return request->reply(400, "text/plain", "missing c");
        if (!Bluetooth::IsConnected())
            return request->reply(409, "text/plain", "not connected");
        String c = request->getParam("c")->value();
        bool ok = true;
        if (c == "play")        AppleMediaService::Play();
        else if (c == "pause")  AppleMediaService::Pause();
        else if (c == "toggle") AppleMediaService::Toggle();
        else if (c == "next")   AppleMediaService::NextTrack();
        else if (c == "prev")   AppleMediaService::PrevTrack();
        else if (c == "vol+")   AppleMediaService::VolumeUp();
        else if (c == "vol-")   AppleMediaService::VolumeDown();
        else ok = false;
        return request->reply(ok ? 200 : 400, "text/plain", ok ? "ok" : "unknown cmd");
    });

    _server.on("/api/log", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "text/plain", Log::dump().c_str());
    });

    // --- CAN logging (configurable ID filter) ---
    _server.on("/api/can/config", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "application/json", CanLog::configJson().c_str());
    });
    _server.on("/api/can/config", HTTP_POST, [](PsychicRequest *request) {
        bool en = request->hasParam("enabled") && request->getParam("enabled")->value() == "1";
        String ids = request->hasParam("ids") ? request->getParam("ids")->value() : String("");
        CanLog::setConfig(en, ids);
        return request->reply(200, "text/plain", "ok");
    });
    _server.on("/api/can/log", HTTP_GET, [](PsychicRequest *request) {
        return request->reply(200, "text/plain", CanLog::dump().c_str());
    });
    _server.on("/api/can/clear", HTTP_POST, [](PsychicRequest *request) {
        CanLog::clear();
        return request->reply(200, "text/plain", "cleared");
    });

    _server.on("/api/log/clear", HTTP_POST, [](PsychicRequest *request) {
        Log::clear();
        return request->reply(200, "text/plain", "cleared");
    });

    _server.on("/emulate/key", HTTP_POST, [this](PsychicRequest *request) {
        if (!request->hasParam("key") || !request->hasParam("hold"))
            return request->reply(400, "text/plain", "Missing key or hold");
        uint16_t keyCode = request->getParam("key")->value().toInt();
        bool isHold = request->getParam("hold")->value() == "1";
        AffaCommon::AffaKey key = static_cast<AffaCommon::AffaKey>(keyCode);
        Serial.printf("[HTTP /emulate/key] keyCode=0x%04X isHold=%d -> calling OnKeyPressed\n", keyCode, isHold);
        _commands.OnKeyPressed(key, isHold);
        String msg = String("Emulated key 0x") + String(keyCode, HEX) + (isHold ? " (Hold)" : " (Press)");
        Serial.printf("[HTTP /emulate/key] done, reply: %s\n", msg.c_str());
        return request->reply(200, "text/plain", msg.c_str());
    });
}
