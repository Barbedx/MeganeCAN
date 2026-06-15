#pragma once
#include <Arduino.h>

// Dashboard UI (extracted verbatim from HttpServerManager.cpp). Served at "/".
static const char DASHBOARD_PAGE[] PROGMEM = R"rawliteral(
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
