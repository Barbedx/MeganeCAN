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

const char *htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MeganeCAN</title>
<style>
:root{--bg:#0f1115;--card:#1b1e24;--mut:#8a909a;--acc:#0a84ff;--line:#2a2e36}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:#e8eaed;font-family:system-ui,Segoe UI,Roboto,sans-serif;font-size:15px}
.wrap{max-width:680px;margin:0 auto;padding:14px}
header{display:flex;align-items:center;justify-content:space-between;padding:6px 2px 10px}
header h1{font-size:20px;margin:0}
.sub{font-size:12px;color:var(--mut)}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin:10px 0}
.muted{color:var(--mut);font-size:13px}
.title{font-size:18px;font-weight:600;margin:3px 0}
button{background:var(--acc);color:#fff;border:0;border-radius:10px;padding:9px 12px;font-size:14px;cursor:pointer;margin:3px 2px}
button.sec{background:#313640}
button.wide{width:100%}
input,select{width:100%;background:#262a31;color:#e8eaed;border:1px solid var(--line);border-radius:10px;padding:9px;margin:4px 0;font-size:14px}
.row{display:flex;gap:6px;flex-wrap:wrap}.row>*{flex:1}
progress{width:100%;height:8px;margin-top:8px}
details{background:var(--card);border:1px solid var(--line);border-radius:14px;margin:10px 0;overflow:hidden}
details>summary{padding:13px 14px;cursor:pointer;font-weight:600;list-style:none}
details>summary::-webkit-details-marker{display:none}
details>summary:before{content:'\25B8 ';color:var(--mut)}
details[open]>summary:before{content:'\25BE '}
details[open]>summary{border-bottom:1px solid var(--line)}
.body{padding:14px}
.kv{font-family:ui-monospace,monospace;font-size:13px;color:var(--mut)}
.notif{border-bottom:1px solid var(--line);padding:7px 0}.notif:last-child{border:0}
label.ck{display:flex;align-items:center;gap:8px;font-size:14px;margin:8px 0}label.ck input{width:auto;margin:0}
hr{border:0;border-top:1px solid var(--line);margin:12px 0}
a{color:var(--acc)}
table{width:100%;border-collapse:collapse;font-family:ui-monospace,monospace;font-size:13px}
td,th{padding:3px 6px}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:#000c;border:1px solid var(--line);padding:9px 16px;border-radius:10px;font-size:13px;opacity:0;transition:.3s;pointer-events:none}
#toast.show{opacity:1}
</style></head><body><div class="wrap">

<header><h1>MeganeCAN</h1><div class="sub" id="hdrStatus">...</div></header>

<div class="card">
  <div class="muted" id="npPlayer">Now Playing</div>
  <div class="title" id="npTitle">-</div>
  <div class="muted" id="npArtist"></div>
  <progress id="npBar" value="0" max="100"></progress>
  <div class="muted" id="npProg"></div>
  <div class="row" style="margin-top:8px">
    <button onclick="cmd('prev')">&#9198;</button>
    <button onclick="cmd('toggle')">&#9199;</button>
    <button onclick="cmd('next')">&#9197;</button>
    <button class="sec" onclick="cmd('vol-')">Vol &minus;</button>
    <button class="sec" onclick="cmd('vol+')">Vol +</button>
  </div>
</div>

<div class="card" id="notifCard" style="display:none">
  <div class="muted">Notifications</div>
  <div id="notifs"></div>
</div>

<details><summary>Bluetooth</summary><div class="body">
  <div class="kv" id="btStatus">...</div>
  <p class="muted">Pair: iPhone &rarr; Settings &rarr; Bluetooth &rarr; <b>MeganeCAN</b>. Reconnects automatically after.</p>
  <button class="sec wide" onclick="if(confirm('Clear BLE bonds? Also forget on the iPhone.'))postf('/clearbonds',{}).then(()=>toast('Bonds cleared'))">Clear bonds / forget phone</button>
</div></details>

<details><summary>WiFi</summary><div class="body">
  <div class="kv" id="wifiStatus">...</div>
  <select id="ssidSel" onchange="g('wifiSsid').value=this.value"><option value="">-- scanning --</option></select>
  <button class="sec" onclick="scanWifi()">Rescan</button>
  <input id="wifiSsid" placeholder="SSID (pick above or type)">
  <input id="wifiPass" type="password" placeholder="Password">
  <input id="wifiIp" placeholder="Static IP (optional, blank = DHCP)">
  <button class="wide" onclick="saveWifi()">Save &amp; reboot</button>
</div></details>

<details><summary>Display &amp; text</summary><div class="body">
  <div class="row">
    <button onclick="postf('/display/state',{enable:1}).then(()=>toast('Display ON'))">Display ON</button>
    <button class="sec" onclick="postf('/display/state',{enable:0}).then(()=>toast('Display OFF'))">Display OFF</button>
  </div>
  <hr>
  <label class="muted">Static text</label>
  <input id="staticTextInput" placeholder="Text">
  <label class="ck"><input type="checkbox" id="staticSave"> Save</label>
  <button class="wide" onclick="showStatic()">Show static text</button>
  <hr>
  <label class="muted">Info popup &mdash; Carminat only (other displays ignore it); 3 lines, &le;8 chars</label>
  <input id="if1" placeholder="line 1" maxlength="8">
  <input id="if2" placeholder="line 2" maxlength="8">
  <input id="if3" placeholder="line 3" maxlength="8">
  <div class="row">
    <button onclick="showInfo()">Show info popup</button>
    <button class="sec" onclick="closeInfo()">Close</button>
  </div>
  <hr>
  <label class="muted">Scroll text</label>
  <input id="welcomeTextInput" placeholder="Text">
  <label class="ck"><input type="checkbox" id="scrollSave"> Save as welcome</label>
  <button class="wide" onclick="showScroll()">Scroll text</button>
  <hr>
  <div class="row"><input id="timeInput" placeholder="Time HHMM" maxlength="4"><button onclick="getq('/settime?time='+encodeURIComponent(g('timeInput').value)).then(()=>toast('Time set'))">Set time</button></div>
  <div class="row"><input id="voltInput" placeholder="Voltage"><button onclick="getq('/setVoltage?voltage='+encodeURIComponent(g('voltInput').value)).then(()=>toast('Voltage set'))">Set V</button></div>
  <label class="ck"><input type="checkbox" id="autoRestoreCheckbox" onchange="getq('/config/restore?enable='+(this.checked?'1':'0')).then(()=>toast('Saved'))"> Auto-restore text on startup</label>
</div></details>

<details><summary>Device settings (restart)</summary><div class="body">
  <form action="/setDisplay" method="POST">
    <label class="muted">Display type</label>
    <select name="type" id="displayTypeSelect">
      <option value="carminat">Carminat (Nav)</option>
      <option value="updatelist">UpdateList (8-segment)</option>
      <option value="updatelist_menu">UpdateList Menu (full-LED)</option>
    </select>
    <button class="wide">Save &amp; restart</button>
  </form>
  <form action="/setbtmode" method="POST">
    <label class="muted">Bluetooth mode</label>
    <select name="mode" id="btModeSelect">
      <option value="ams">AMS (Apple Media Service)</option>
      <option value="keyboard">BLE Keyboard</option>
    </select>
    <label class="ck"><input type="checkbox" id="autoTimeCheckbox" name="auto_time" value="1"> Auto-sync time from phone</label>
    <input type="hidden" name="auto_time" value="0">
    <button class="wide">Save &amp; restart</button>
  </form>
  <form action="/setskipfuncreg" method="POST">
    <label class="ck"><input type="checkbox" id="skipFuncRegCheckbox" name="skip_funcreg" value="1"> Skip function registration (real radio)</label>
    <input type="hidden" name="skip_funcreg" value="0">
    <button class="wide sec">Save &amp; restart</button>
  </form>
  <form action="/setelm" method="POST">
    <label class="ck"><input type="checkbox" id="elmEnabledCheckbox" name="elm_enabled" value="1"> Enable ELM327 / OBD (V-LINK WiFi)</label>
    <input type="hidden" name="elm_enabled" value="0">
    <button class="wide sec">Save &amp; restart</button>
  </form>
</div></details>

<details><summary>OBD / ELM live</summary><div class="body">
  <div class="row"><button class="sec" onclick="refreshLive()">Refresh</button>
    <label class="ck"><input type="checkbox" id="autoRefresh" checked onchange="toggleAuto(this.checked)"> Auto 2s</label></div>
  <table><thead><tr class="muted"><th align="left">Description</th><th>Code</th><th align="right">Value</th></tr></thead>
    <tbody id="liveBody"><tr><td colspan="3" class="muted">-</td></tr></tbody></table>
  <div id="elmHeaders" class="muted" style="margin-top:8px">...</div>
</div></details>

<details><summary>Button emulation</summary><div class="body">
  <button class="sec wide" onclick="postf('/setaux',{}).then(()=>toast('AUX set'))">Set AUX mode</button>
  <div id="keys" style="margin-top:8px"></div>
  <div class="muted" id="keyResult"></div>
</div></details>

<details><summary>CAN logging</summary><div class="body">
  <label class="ck"><input type="checkbox" id="canEn"> Enable CAN logging</label>
  <input id="canIds" placeholder="ID filter (hex, comma-sep; blank = all) e.g. 151,3CF">
  <div class="row"><button onclick="saveCan()">Save</button><button class="sec" onclick="loadCan();refreshCanSeen()">Refresh</button></div>
  <div class="row">
    <a href="/api/can/log" download="can.log" style="flex:1"><button class="sec wide">Download CAN log</button></a>
    <button class="sec" onclick="postf('/api/can/clear',{}).then(()=>{toast('CAN cleared');refreshCanSeen();})">Clear</button>
  </div>
  <div class="muted" style="margin-top:8px">Seen IDs (tap to add to filter):</div>
  <div id="canSeen"></div>
</div></details>

<details><summary>System</summary><div class="body">
  <p><a href="/api/log" download="meganecan.log">Download log &darr;</a></p>
  <button class="sec wide" onclick="postf('/api/log/clear',{}).then(()=>toast('Log cleared'))">Clear log</button>
  <hr>
  <p><a href="/update">OTA firmware update &rarr;</a></p>
  <button class="sec wide" onclick="if(confirm('Restart device?'))postf('/restart',{})">Restart ESP32</button>
</div></details>

</div><div id="toast"></div>
<script>
const g=id=>document.getElementById(id);
const esc=t=>(t||'').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
let _tt;function toast(m){const t=g('toast');t.textContent=m;t.classList.add('show');clearTimeout(_tt);_tt=setTimeout(()=>t.classList.remove('show'),1800);}
async function getq(u){try{return await(await fetch(u)).text();}catch(e){toast('error');}}
async function postf(u,o){const body=new URLSearchParams(o);try{return await(await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})).text();}catch(e){toast('error');}}
function cmd(c){fetch('/api/cmd?c='+encodeURIComponent(c));}
function mmss(s){s=Math.max(0,Math.floor(s));return Math.floor(s/60)+':'+('0'+(s%60)).slice(-2);}
function showStatic(){let u='/static?text='+encodeURIComponent(g('staticTextInput').value);if(g('staticSave').checked)u+='&save=on';getq(u).then(()=>toast('Shown'));}
function showScroll(){let u='/scroll?text='+encodeURIComponent(g('welcomeTextInput').value);if(g('scrollSave').checked)u+='&save=on';getq(u).then(()=>toast('Scrolling'));}
function showInfo(){getq('/api/info?l1='+encodeURIComponent(g('if1').value)+'&l2='+encodeURIComponent(g('if2').value)+'&l3='+encodeURIComponent(g('if3').value)).then(()=>toast('Info popup'));}
function closeInfo(){getq('/api/info/close').then(()=>toast('Info close'));}

function renderMedia(d){try{
  if(!d.connected){g('npPlayer').textContent='Not connected';g('npTitle').textContent='-';g('npArtist').textContent='';g('npBar').value=0;g('npProg').textContent='';return;}
  g('npPlayer').textContent=(d.player||'')+' · '+d.state+' · vol '+Math.round(d.volume*100)+'%';
  g('npTitle').textContent=d.title||'-';
  g('npArtist').textContent=(d.artist||'')+(d.album?' — '+d.album:'');
  g('npBar').value=d.duration>0?100*d.elapsed/d.duration:0;
  g('npProg').textContent=mmss(d.elapsed)+' / '+mmss(d.duration)+'  ·  '+(d.queueIndex+1)+'/'+d.queueCount;
}catch(e){}}
function renderNotifs(l){try{
  g('notifCard').style.display=l.length?'block':'none';let h='';
  l.forEach(n=>{h+='<div class="notif"><b>'+esc(n.title||'(no title)')+'</b><br><span class="muted">'+esc(n.msg||'')+'</span><br><span class="muted">'+esc(n.cat+' · '+n.app)+'</span></div>';});
  g('notifs').innerHTML=h;}catch(e){}}
function renderBt(d){try{
  g('btStatus').innerHTML='Status: '+esc(d.status)+'<br>Connected: '+(d.connected?'yes '+esc(d.address||''):'no')+'<br>Bonded: '+(d.bonded?'yes':'no');
  g('hdrStatus').textContent=(d.connected?'BT connected':'BT '+d.status);}catch(e){}}
function renderWifi(d){try{
  g('wifiStatus').innerHTML='Mode: '+esc(d.mode)+' · SSID: '+esc(d.ssid)+'<br>IP: '+esc(d.ip)+' · http://'+esc(d.host)+'.local';}catch(e){}}
async function scanWifi(){const sel=g('ssidSel');sel.innerHTML='<option value="">-- scanning --</option>';await fetch('/api/wifi/scan');
  let n=0,t=setInterval(async()=>{const d=await(await fetch('/api/wifi/networks')).json();
    if(!d.scanning){clearInterval(t);sel.innerHTML='<option value="">-- choose --</option>';
      d.nets.forEach(x=>{const o=document.createElement('option');o.value=x.ssid;o.textContent=x.ssid+' ('+x.rssi+')'+(x.secure?' *':'');sel.appendChild(o);});}
    if(++n>10)clearInterval(t);},1200);}
async function saveWifi(){toast(await postf('/api/wifi/save',{ssid:g('wifiSsid').value,pass:g('wifiPass').value,ip:g('wifiIp').value}));}

let _liveTimer=null;
async function refreshLive(){const r=await fetch('/api/live/full');const tb=g('liveBody');
  if(!r.ok){tb.innerHTML='<tr><td colspan="3" style="color:#e55">ELM not connected</td></tr>';return;}
  const data=await r.json();let h='';
  for(const m of data){const v=m.hasValue?(parseFloat(m.value).toFixed(2)+(m.unit?' '+m.unit:'')):'-';
    h+='<tr><td>'+esc(m.name)+'</td><td align="center">'+esc(m.shortName)+'</td><td align="right">'+v+'</td></tr>';}
  tb.innerHTML=h||'<tr><td colspan="3" class="muted">No metrics</td></tr>';}
function toggleAuto(on){if(_liveTimer){clearInterval(_liveTimer);_liveTimer=null;}if(on)_liveTimer=setInterval(refreshLive,2000);}
async function loadElmHeaders(){const r=await fetch('/api/elm/headers');if(!r.ok){g('elmHeaders').textContent='ELM not available';return;}
  const data=await r.json();let h='';for(const[hdr,en]of Object.entries(data)){
    h+='<label class="ck"><input type="checkbox" '+(en?'checked':'')+' onchange="setHeader(\''+hdr+'\',this.checked)"> '+hdr+'</label>';}
  g('elmHeaders').innerHTML=h;}
async function setHeader(hdr,en){await postf('/api/elm/headers',{header:hdr,enabled:en?'1':'0'});}

const KEYS=[['Load',0],['Roll Up',257],['Roll Down',321],['Pause',5],['Src >',1],['Src <',2],['Vol +',3],['Vol -',4]];
function buildKeys(){let h='';KEYS.forEach(k=>{h+='<div class="row"><button onclick="sendKey('+k[1]+',0)">'+k[0]+'</button><button class="sec" onclick="sendKey('+k[1]+',1)">'+k[0]+' (hold)</button></div>';});g('keys').innerHTML=h;}
async function sendKey(key,hold){g('keyResult').textContent=await postf('/emulate/key',{key,hold:hold?'1':'0'});}

async function loadConfig(){try{
  g('elmEnabledCheckbox').checked=(await getq('/getelmenabled'))==='1';
  const dt=await getq('/getdisplaytype');[...g('displayTypeSelect').options].forEach((o,i)=>{if(o.value===dt)g('displayTypeSelect').selectedIndex=i;});
  const bm=await getq('/getbtmode');[...g('btModeSelect').options].forEach((o,i)=>{if(o.value===bm)g('btModeSelect').selectedIndex=i;});
  g('autoTimeCheckbox').checked=(await getq('/getautotime'))==='1';
  g('skipFuncRegCheckbox').checked=(await getq('/getskipfuncreg'))==='1';
  g('autoRestoreCheckbox').checked=(await getq('/config/restore'))==='1';
  g('staticTextInput').value=await getq('/getlasttext');
  g('welcomeTextInput').value=await getq('/getwelcometext');
}catch(e){}}

async function loadCan(){const d=await(await fetch('/api/can/config')).json();g('canEn').checked=d.enabled;g('canIds').value=d.filter||'';}
function renderCanSeen(d){try{const seen=(d.seen||[]).sort((a,b)=>b.n-a.n);
  let h='';seen.forEach(s=>{h+='<button class="sec" style="margin:2px;padding:5px 8px" onclick="addId(\''+s.id+'\')">'+s.id+' ('+s.n+')</button>';});
  g('canSeen').innerHTML=h||'<span class="muted">none yet</span>';}catch(e){}}
async function refreshCanSeen(){try{const d=await(await fetch('/api/can/config')).json();renderCanSeen(d);}catch(e){}}
async function refreshDashboard(){try{const d=await(await fetch('/api/dashboard')).json();
  renderMedia(d.media);renderNotifs(d.notifs);renderBt(d.bt);renderWifi(d.wifi);renderCanSeen(d.can);}catch(e){}}
function addId(id){const f=g('canIds');const cur=f.value.split(/[,\s]+/).filter(Boolean);if(!cur.includes(id))cur.push(id);f.value=cur.join(',');}
async function saveCan(){await postf('/api/can/config',{enabled:g('canEn').checked?'1':'0',ids:g('canIds').value});toast('CAN config saved');}

buildKeys();loadConfig();loadElmHeaders();loadCan();
refreshDashboard();
setInterval(refreshDashboard,2500);
</script></body></html>
)rawliteral";

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
               { return request->reply(200, "text/html", htmlPage); });

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
        char j[320];
        snprintf(j, sizeof(j),
            "{\"heap\":{\"free\":%u,\"min\":%u,\"maxblk\":%u,\"total\":%u},"
            "\"uptime_ms\":%lu,\"ws\":{\"clients\":%d,\"dropped\":%lu}}",
            (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
            (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getHeapSize(),
            (unsigned long)millis(),
            _wire ? _wire->clientCount() : 0,
            (unsigned long)(_wire ? _wire->dropped() : 0));
        return request->reply(200, "application/json", j);
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
