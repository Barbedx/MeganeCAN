#include "HttpServerManager.h"
#include "effects/ScrollEffect.h"
#include "../commands/DisplayCommands.h"
#include "../bluetooth.h"
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
    // Fetch ELM enabled
    const elmRes = await fetch('/getelmenabled');
    const elmEnabled = await elmRes.text();
    document.getElementById('elmEnabledCheckbox').checked = (elmEnabled === '1');

    // Fetch display type and pre-select the correct option
    const dtRes = await fetch('/getdisplaytype');
    const displayType = await dtRes.text();
    const sel = document.getElementById('displayTypeSelect');
    for (let i = 0; i < sel.options.length; i++) {
      if (sel.options[i].value === displayType) {
        sel.selectedIndex = i;
        break;
      }
    }

    // Fetch BT mode
    const btRes = await fetch('/getbtmode');
    const btMode = await btRes.text();
    const btSel = document.getElementById('btModeSelect');
    for (let i = 0; i < btSel.options.length; i++) {
      if (btSel.options[i].value === btMode) {
        btSel.selectedIndex = i;
        break;
      }
    }

    // Fetch auto_time
    const atRes = await fetch('/getautotime');
    const autoTime = await atRes.text();
    document.getElementById('autoTimeCheckbox').checked = (autoTime === '1');

    // Fetch skip_funcreg
    const sfrRes = await fetch('/getskipfuncreg');
    const sfr = await sfrRes.text();
    document.getElementById('skipFuncRegCheckbox').checked = (sfr === '1');

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

  <h2>Display Type</h2>
    <form action="/setDisplay" method="POST">
    <select name="type" id="displayTypeSelect">
        <option value="carminat">Carminat (Nav)</option>
        <option value="updatelist">UpdateList (8-segment)</option>
        <option value="updatelist_menu">UpdateList Menu (full-LED)</option>
    </select>
    <input type="submit" value="Save &amp; Restart" />
    </form>
    <form action="/setskipfuncreg" method="POST" style="margin-top:6px">
    <label>
        <input type="checkbox" id="skipFuncRegCheckbox" name="skip_funcreg" value="1" />
        Skip function registration (connected to real radio)
    </label>
    <input type="hidden" name="skip_funcreg" value="0" />
    <input type="submit" value="Save &amp; Restart" />
    </form>

  <h2>Bluetooth Mode</h2>
    <form action="/setbtmode" method="POST">
    <select name="mode" id="btModeSelect">
        <option value="ams">AMS (Apple Media Service)</option>
        <option value="keyboard">BLE Keyboard</option>
    </select>
    <label style="margin-left:12px">
        <input type="checkbox" id="autoTimeCheckbox" name="auto_time" value="1" />
        Auto-sync time from phone
    </label>
    <input type="hidden" name="auto_time" value="0" />
    <input type="submit" value="Save &amp; Restart" />
    </form>

  <h2>Auto-restore text</h2>
    <form action="/config/restore" method="GET">
    <label>
        <input type="checkbox" name="enable" id="autoRestoreCheckbox" value="1" />
        Auto-Restore texts on startup
    </label>
    <!-- Hidden input sends 0 when checkbox is unchecked -->
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

    <h2>Set Voltage</h2>
    <form action="/setVoltage" method="GET">
    <label>Voltage:</label>
    <input type="text" name="voltage" required />
    <input type="submit" value="Set Voltage" />
    </form>

  <hr>

  <h2>ELM327 / OBD Diagnostics</h2>
    <form action="/setelm" method="POST">
    <label>
        <input type="checkbox" id="elmEnabledCheckbox" name="elm_enabled" value="1" />
        Enable ELM327 (connects to V-LINK WiFi on boot, fetches voltage &amp; sensor data)
    </label>
    <input type="hidden" name="elm_enabled" value="0" />
    <input type="submit" value="Save &amp; Restart" />
    </form>
    <h3>Live OBD Data</h3>
    <div style="margin-bottom:6px">
      <button onclick="refreshLive()">Refresh</button>
      <label style="margin-left:12px"><input type="checkbox" id="autoRefresh" checked onchange="toggleAuto(this.checked)"> Auto-refresh (2s)</label>
    </div>
    <table id="liveTable" style="border-collapse:collapse;font-family:monospace;font-size:13px">
      <thead><tr style="background:#ddd">
        <th style="padding:4px 8px;text-align:left">Description</th>
        <th style="padding:4px 8px">Code</th>
        <th style="padding:4px 8px">Label</th>
        <th style="padding:4px 8px;text-align:right">Value</th>
      </tr></thead>
      <tbody id="liveBody"><tr><td colspan="4" style="padding:4px 8px">Loading...</td></tr></tbody>
    </table>
    <script>
    let _liveTimer = null;
    async function refreshLive() {
      const res = await fetch('/api/live/full');
      const tbody = document.getElementById('liveBody');
      if (!res.ok) { tbody.innerHTML = '<tr><td colspan="4" style="padding:4px 8px;color:red">ELM not connected</td></tr>'; return; }
      const data = await res.json();
      let html = '';
      for (const m of data) {
        const val = m.hasValue ? (parseFloat(m.value).toFixed(2) + (m.unit ? '\xA0' + m.unit : '')) : '\u2014';
        const clr = m.hasValue ? '' : 'color:#aaa';
        html += '<tr style="' + clr + '">'
          + '<td style="padding:2px 8px">' + m.name + '</td>'
          + '<td style="padding:2px 8px;text-align:center">' + m.shortName + '</td>'
          + '<td style="padding:2px 8px;text-align:center">' + m.label + '</td>'
          + '<td style="padding:2px 8px;text-align:right">' + val + '</td>'
          + '</tr>';
      }
      tbody.innerHTML = html || '<tr><td colspan="4" style="padding:4px 8px">No metrics</td></tr>';
    }
    function toggleAuto(on) {
      if (_liveTimer) { clearInterval(_liveTimer); _liveTimer = null; }
      if (on) _liveTimer = setInterval(refreshLive, 2000);
    }
    window.addEventListener('DOMContentLoaded', () => { refreshLive(); _liveTimer = setInterval(refreshLive, 2000); });
    </script>

  <hr>

  <h2>ELM Headers</h2>
    <div id="elmHeaders">Loading...</div>
    <script>
    async function loadElmHeaders() {
      const res = await fetch('/api/elm/headers');
      if (!res.ok) { document.getElementById('elmHeaders').textContent = 'ELM not available'; return; }
      const data = await res.json();
      const div = document.getElementById('elmHeaders');
      div.innerHTML = '';
      for (const [hdr, en] of Object.entries(data)) {
        const id = 'hdr_' + hdr;
        div.innerHTML += '<label><input type="checkbox" id="' + id + '" ' + (en ? 'checked' : '') +
          ' onchange="setHeader(\'' + hdr + '\',this.checked)"> ' + hdr + '</label><br>';
      }
    }
    async function setHeader(hdr, enabled) {
      const body = new URLSearchParams({header: hdr, enabled: enabled ? '1' : '0'});
      await fetch('/api/elm/headers', {method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body});
    }
    window.addEventListener('DOMContentLoaded', loadElmHeaders);
    </script>

  <hr>

  <h2>Bluetooth</h2>
    <div id="btStatus" style="font-family:monospace;background:#f4f4f4;padding:8px;margin-bottom:8px;">Loading BT status...</div>
    <form action="/clearbonds" method="POST"
          onsubmit="return confirm('Clear BLE bonds? You will need to re-pair on the iPhone too.');">
    <input type="submit" value="Clear BLE Bonds" />
    </form>
    <form action="/forgetdevice" method="POST" style="margin-top:6px"
          onsubmit="return confirm('Forget saved device? Clears preferred address and bonds. Next boot scans fresh.');">
    <input type="submit" value="Forget Saved Device" />
    </form>
    <script>
    async function tryCandidate(idx) {
      await fetch('/bt/try?idx=' + idx, {method:'POST'});
    }
    async function refreshBt() {
      try {
        const r = await fetch('/api/bt');
        const d = await r.json();
        let html = '<b>Status:</b> ' + d.status + '<br>';
        if (d.candidates && d.candidates.length > 0) {
          html += '<b>Discovered Apple devices (RSSI = signal strength, higher = closer):</b>';
          html += '<table style="border-collapse:collapse;font-size:13px;margin-top:4px">';
          html += '<tr style="background:#ddd"><th>#</th><th>Addr</th><th>Name</th><th>RSSI</th><th>Type</th><th></th></tr>';
          for (let i = 0; i < d.candidates.length; i++) {
            const c = d.candidates[i];
            const sel = c.selected;
            html += '<tr style="' + (sel ? 'font-weight:bold;background:#fffacc' : '') + '">';
            html += '<td style="padding:2px 6px">' + (i+1) + '</td>';
            html += '<td style="padding:2px 6px;font-family:monospace">' + c.addr + '</td>';
            html += '<td style="padding:2px 6px">' + (c.name || '<i>(no name)</i>') + '</td>';
            html += '<td style="padding:2px 6px">' + c.rssi + ' dBm</td>';
            html += '<td style="padding:2px 6px;font-family:monospace">' + (c.type || '?') + '</td>';
            html += '<td style="padding:2px 4px"><button onclick="tryCandidate(' + i + ')">Try</button></td>';
            html += '</tr>';
          }
          html += '</table>';
        } else {
          html += '<i>No Apple devices found yet</i>';
        }
        document.getElementById('btStatus').innerHTML = html;
      } catch(e) { console.error('BT status fetch failed', e); document.getElementById('btStatus').textContent = 'BT status unavailable'; }
    }
    refreshBt();
    setInterval(refreshBt, 3000);
    </script>

  <hr>
    <h2>OTA Update</h2>
    <p>Use the <a href="/update">OTA Update</a> link to upload new firmware.</p>

  <hr>
  <h2>Button emulation</h2>
  <div>
    <form action="/setaux" method="POST" style="display:inline">
      <button type="submit">Set AUX mode</button>
    </form>
  </div><br>
  <div>
    <button onclick="sendKey(0x0000,0)">Load</button>
    <button onclick="sendKey(0x0000,1)">Load (Hold)</button><br><br>
    <button onclick="sendKey(0x0101,0)">Roll Up</button>
    <button onclick="sendKey(0x0141,0)">Roll Down</button><br><br>
    <button onclick="sendKey(0x0005,0)">Pause</button>
    <button onclick="sendKey(0x0001,0)">Src&gt;</button>
    <button onclick="sendKey(0x0002,0)">Src&lt;</button><br><br>
    <button onclick="sendKey(0x0003,0)">Vol+</button>
    <button onclick="sendKey(0x0004,0)">Vol-</button>
  </div>
  <script>
  async function sendKey(key, hold) {
    const body = new URLSearchParams({key, hold: hold ? '1' : '0'});
    const r = await fetch('/emulate/key', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body});
    console.log(await r.text());
  }
  </script>

  <hr>
  <h2>System</h2>
  <form action="/restart" method="POST" onsubmit="return confirm('Restart device?');">
    <input type="submit" value="Restart ESP32" />
  </form>
</body>
</html>
)rawliteral";

void HttpServerManager::begin()
{
    _server.config.max_uri_handlers = 32;
    _server.listen(80);

    ElegantOTA.begin(&_server);
    setupRoutes();
    Serial.println("HTTP Server: routes initialized.");
}

void HttpServerManager::setupRoutes()
{
    _server.on("/", HTTP_GET, [this](PsychicRequest *request)
               { return request->reply(200, "text/html", htmlPage); });

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
        Preferences prefs;
        prefs.begin("config", true);
        String dt = prefs.getString("display_type", "carminat");
        prefs.end();
        return request->reply(200, "text/plain", dt.c_str());
    });

    _server.on("/getbtmode", HTTP_GET, [](PsychicRequest *request) {
        Preferences prefs;
        prefs.begin("config", true);
        String mode = prefs.getString("bt_mode", "ams");
        prefs.end();
        return request->reply(200, "text/plain", mode.c_str());
    });

    _server.on("/getautotime", HTTP_GET, [](PsychicRequest *request) {
        Preferences prefs;
        prefs.begin("config", true);
        bool at = prefs.getBool("auto_time", true);
        prefs.end();
        return request->reply(200, "text/plain", at ? "1" : "0");
    });

    _server.on("/getskipfuncreg", HTTP_GET, [](PsychicRequest *request) {
        Preferences prefs;
        prefs.begin("config", true);
        bool v = prefs.getBool("skip_funcreg", false);
        prefs.end();
        return request->reply(200, "text/plain", v ? "1" : "0");
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
        Bluetooth::ForgetDevice();
        return request->reply(200, "text/plain", "Saved device forgotten. Scanning fresh.");
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
        const char *page = R"rawliteral(
<!DOCTYPE html><html><head><title>Diagnostics</title>
<style>
  body { font-family: sans-serif; max-width: 640px; margin: 2em auto; }
  select, input { margin: 4px; padding: 4px; }
  #result { background: #111; color: #0f0; padding: 1em; white-space: pre; min-height: 4em; border-radius: 4px; }
  .row { margin: 8px 0; }
</style>
</head><body>
<h1>On-Demand PID Scan</h1>

<div class="row">
  <label>Header: <select id="hdrSel" onchange="populatePids()"><option>Loading...</option></select></label>
  <label>PID: <select id="pidSel"><option>-</option></select></label>
  <button onclick="doScan()">Scan</button>
</div>

<div class="row">
  <label>Manual &mdash; Header: <input id="manHdr" size="5" placeholder="7E0"></label>
  <label>PID: <input id="manPid" size="6" placeholder="21A0"></label>
  <button onclick="doManualScan()">Scan</button>
</div>

<div id="result">Ready.</div>

<script>
let planData = {};

async function loadPlan() {
  try {
    const r = await fetch('/api/elm/plan');
    planData = await r.json();
    const sel = document.getElementById('hdrSel');
    sel.innerHTML = '';
    for (const h of Object.keys(planData)) {
      const o = document.createElement('option'); o.value = h; o.textContent = h;
      sel.appendChild(o);
    }
    populatePids();
  } catch(e) { document.getElementById('result').textContent = 'Error loading plan: ' + e; }
}

function populatePids() {
  const hdr = document.getElementById('hdrSel').value;
  const sel = document.getElementById('pidSel');
  sel.innerHTML = '';
  for (const p of (planData[hdr] || [])) {
    const o = document.createElement('option'); o.value = p; o.textContent = p;
    sel.appendChild(o);
  }
}

async function scan(header, pid) {
  document.getElementById('result').textContent = 'Scanning ' + header + ' / ' + pid + ' ...';
  try {
    const body = new URLSearchParams({header, pid});
    const r = await fetch('/api/elm/scan', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body
    });
    const data = await r.json();
    document.getElementById('result').textContent = JSON.stringify(data, null, 2);
  } catch(e) { document.getElementById('result').textContent = 'Error: ' + e; }
}

function doScan() {
  scan(document.getElementById('hdrSel').value, document.getElementById('pidSel').value);
}
function doManualScan() {
  const h = document.getElementById('manHdr').value.trim();
  const p = document.getElementById('manPid').value.trim();
  if (h && p) scan(h, p); else alert('Enter both header and PID');
}

window.addEventListener('DOMContentLoaded', loadPlan);
</script>
</body></html>
        )rawliteral";
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
        return request->reply(200, "text/html", page);
    });

    _server.on("/emulate", HTTP_GET, [](PsychicRequest *request) {
        const char *page = R"rawliteral(
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

    _server.on("/setaux", HTTP_POST, [this](PsychicRequest *request) {
        _display.setAuxMode(true);
        return request->reply(200, "text/plain", "AUX mode activated");
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

    _server.on("/bt/try", HTTP_POST, [](PsychicRequest *request) {
        if (!request->hasParam("idx"))
            return request->reply(400, "text/plain", "Missing idx");
        int idx = request->getParam("idx")->value().toInt();
        Bluetooth::SelectByIndex(idx);
        return request->reply(200, "text/plain", "Trying device");
    });

    _server.on("/emulate/key", HTTP_POST, [this](PsychicRequest *request) {
        if (!request->hasParam("key") || !request->hasParam("hold"))
            return request->reply(400, "text/plain", "Missing key or hold");
        uint16_t keyCode = request->getParam("key")->value().toInt();
        bool isHold = request->getParam("hold")->value() == "1";
        AffaCommon::AffaKey key = static_cast<AffaCommon::AffaKey>(keyCode);
        _commands.OnKeyPressed(key, isHold);
        String msg = String("Emulated key 0x") + String(keyCode, HEX) + (isHold ? " (Hold)" : " (Press)");
        return request->reply(200, "text/plain", msg.c_str());
    });
}
