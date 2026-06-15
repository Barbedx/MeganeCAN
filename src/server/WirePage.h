#pragma once
#include <Arduino.h>

// Self-contained wireless CAN viewer + display steering, served at /wire. Opens a
// WebSocket to /canstream (the WsWireLink stream), renders the live @TX/@RX frames
// with a SavvyCAN-style ID filter + per-ID table, decodes the AFFA3 screen (same
// semantics as tools/serial_proxy.py), steers the display over HTTP (/emulate/key,
// /api/emu, /setaux), and records the stream to a downloadable .canlog (the exact
// format ReplayCanBus parses). Works from any phone/PC browser on the ESP's network
// — no laptop tool required.
static const char WIRE_PAGE[] PROGMEM = R"WIRE(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>MeganeCAN Wire</title><style>
*{box-sizing:border-box}body{margin:0;font:13px/1.4 system-ui,monospace;background:#11131a;color:#cdd6f4}
header{display:flex;align-items:center;gap:10px;padding:8px 12px;background:#181a25;position:sticky;top:0}
#dot{width:10px;height:10px;border-radius:50%;background:#f38ba8}#dot.on{background:#a6e3a1}
.wrap{display:grid;grid-template-columns:1fr 1fr;gap:10px;padding:10px}@media(max-width:760px){.wrap{grid-template-columns:1fr}}
.card{background:#181a25;border:1px solid #2a2d3a;border-radius:8px;padding:8px}
h3{margin:0 0 6px;font-size:12px;color:#89b4fa;text-transform:uppercase;letter-spacing:.05em}
#log{height:38vh;overflow:auto;font-family:ui-monospace,monospace;font-size:12px;white-space:pre;background:#0e0f16;border-radius:6px;padding:6px}
#log .tx{color:#a6e3a1}#log .rx{color:#fab387}#log mark{background:#f9e2af;color:#000}
table{width:100%;border-collapse:collapse;font-size:12px}td,th{padding:2px 6px;text-align:left;border-bottom:1px solid #23263300}
#ids tr{cursor:pointer}#ids tr:hover{background:#23263a}
input,button,select{font:inherit;background:#23263a;color:#cdd6f4;border:1px solid #2a2d3a;border-radius:6px;padding:5px 8px}
button{cursor:pointer}button:hover{background:#313244}button.k{min-width:64px}
.row{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin:4px 0}
.scr{background:#0b3d2e;border:1px solid #1e6b50;border-radius:8px;padding:10px;font-family:ui-monospace,monospace}
.scr .hdr{color:#a6e3a1;font-weight:700;border-bottom:1px solid #1e6b50;padding-bottom:4px;margin-bottom:6px}
.scr .it{padding:2px 0}.scr .it.sel{background:#1e6b50;color:#fff}
small{color:#6c7086}
</style></head><body>
<header><span id=dot></span><b>MeganeCAN&nbsp;Wire</b>
<span id=stat><small>connecting…</small></span>
<span style=margin-left:auto></span>
<button id=recBtn onclick=toggleRec()>● Record</button>
<button onclick=save()>Save .canlog</button>
<button onclick=clr()>Clear</button></header>

<div class=wrap>
  <div class=card>
    <h3>Live frames</h3>
    <div class=row>
      <input id=flt placeholder="filter id/text (regex)" oninput=mkFilter() style=flex:1>
      <label><input type=checkbox id=pause> pause</label>
    </div>
    <div id=log></div>
    <small id=cnt></small>
  </div>

  <div class=card>
    <h3>Decoded screen</h3>
    <div class=scr id=screen><div class=hdr>— display —</div><div class=it>waiting…</div></div>
    <h3 style=margin-top:10px>Steer display</h3>
    <div class=row>
      <button class=k onclick="key(0,1)">Menu</button>
      <button class=k onclick="key(257,0)">▲ Up</button>
      <button class=k onclick="key(321,0)">▼ Down</button>
      <button class=k onclick="key(3,0)">Vol+</button>
      <button class=k onclick="key(4,0)">Vol−</button>
      <button class=k onclick="key(1,0)">Src▶</button>
      <button class=k onclick="key(2,0)">◀Src</button>
    </div>
    <div class=row>
      <button onclick="emu(1)">Self-ACK ON</button>
      <button onclick="emu(0)">OFF</button>
      <button onclick="getq('/setaux')">Set AUX</button>
    </div>
    <div class=row>
      <input id=raw placeholder="@INJ 3CF 61 11 / @KEY 0 1 / @EMU 1" style=flex:1>
      <button onclick=sendRaw()>WS send</button>
    </div>
    <h3 style=margin-top:10px>IDs seen</h3>
    <table id=idsT><thead><tr><th>ID</th><th>#</th><th>last data</th></tr></thead><tbody id=ids></tbody></table>
  </div>
</div>

<script>
const $=s=>document.getElementById(s);
let re=null,seen={},frames=0,rec=false,recBuf=[],recT0=0;
function mkFilter(){try{re=$('flt').value?new RegExp($('flt').value,'i'):null;$('flt').style.color=''}catch(e){$('flt').style.color='#f38ba8'}}
function clr(){$('log').textContent='';seen={};$('ids').innerHTML='';frames=0;$('cnt').textContent=''}
function atBottom(){const l=$('log');return l.scrollHeight-l.scrollTop-l.clientHeight<40}
function addLog(line,cls){
  if(re&&!re.test(line))return;
  if($('pause').checked)return;
  const st=atBottom(),d=document.createElement('div');d.className=cls;
  if(re){d.innerHTML=line.replace(/</g,'&lt;').replace(re,m=>'<mark>'+m+'</mark>')}else{d.textContent=line}
  const lg=$('log');lg.appendChild(d);while(lg.childElementCount>3000)lg.removeChild(lg.firstChild);
  if(st)lg.scrollTop=lg.scrollHeight;
}
function noteId(id,bytes){
  const e=seen[id]||(seen[id]={n:0,last:'',row:null});e.n++;e.last=bytes.map(b=>b.toString(16).padStart(2,'0').toUpperCase()).join(' ');
  if(!e.row){e.row=document.createElement('tr');e.row.onclick=()=>{$('flt').value=id;mkFilter()};
    e.row.innerHTML='<td>'+id+'</td><td class=c></td><td class=d></td>';$('ids').appendChild(e.row);}
  e.row.children[1].textContent=e.n;e.row.children[2].textContent=e.last;
}
// ---- AFFA3 screen decode (ported from serial_proxy.py) ----
let asm=[],asmActive=false,mode=null;const scr={hdr:'',items:[],sel:null,scroll:0};let info={},infoPend=null;
function asciiz(p,a,z){let s='';for(let i=a;i<=z&&i<p.length;i++){const c=p[i];if(c===0)break;if(c>=32&&c<127)s+=String.fromCharCode(c)}return s.trim()}
function bytesTxt(a){return a.map(c=>(c>=32&&c<127)?String.fromCharCode(c):'').join('').trim()}
function decodeMenu(p){if(p.length<96)return;scr.scroll=p[10];scr.hdr=asciiz(p,11,36)||'(menu)';scr.items=[{id:p[38],text:asciiz(p,39,63)},{id:p[65],text:asciiz(p,66,95)}];renderMenu()}
function onTx(id,b){
  if(id!=='151')return;const b0=b[0];
  if(b0===0x10){
    if(b[1]===0x5A){mode='menu';asm=b.slice(0);asmActive=true;scr.sel=null;decodeMenu(asm);return}
    if(b[1]===0x0B&&b[2]===0x76){mode='info';asmActive=false;if(b[4]===0x41)info={};infoPend={off:b[4],chars:b.slice(5,8)};return}
    asmActive=false;return;
  }
  if((b0&0xF0)===0x20){
    if(mode==='menu'&&asmActive){asm=asm.concat(b.slice(1));decodeMenu(asm);return}
    if(mode==='info'&&infoPend){infoPend.chars=infoPend.chars.concat(b.slice(1,6));info[infoPend.off]=bytesTxt(infoPend.chars);infoPend=null;renderInfo();return}
    return;
  }
  if(b0===0x07&&b[1]===0x29&&b[2]===0x01){scr.sel=b[3];if(mode==='menu')renderMenu();return}
}
function paint(hdr,arrow,lines,meta){
  let h='<div class=hdr>'+esc(arrow+' '+hdr)+'</div>';
  lines.forEach(l=>h+='<div class="it'+(l.sel?' sel':'')+'">'+esc((l.sel?'▶ ':'  ')+(l.text||''))+'</div>');
  h+='<small>'+esc(meta)+'</small>';$('screen').innerHTML=h;
}
function esc(s){return(s||'').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
function renderMenu(){paint(scr.hdr||'— display —',scr.scroll===0x0B?'↓':scr.scroll===0x07?'↑':scr.scroll===0x0C?'↕':'',scr.items.filter(it=>it.text||it.id).map(it=>({text:it.text,sel:it.id===scr.sel})),'0x151 showMenu '+asm.length+'B')}
function renderInfo(){const o=Object.keys(info).map(Number).sort((a,b)=>a-b);paint('ⓘ Info popup','',o.map(k=>({text:info[k],sel:false})),'0x151 showInfoMenu')}
// ---- stream ----
function handle(line){
  const m=line.match(/@(TX|RX)\s+([0-9A-Fa-f]+)\s+(.*)$/);
  if(m){const id=m[2].toUpperCase().replace(/^0+(?=.)/,'');const bytes=m[3].trim().split(/\s+/).map(h=>parseInt(h,16)).filter(n=>!isNaN(n));
    addLog(line,m[1]==='TX'?'tx':'rx');noteId(id,bytes);if(m[1]==='TX')try{onTx(id,bytes)}catch(e){}}
  else addLog(line,'');
  frames++;$('cnt').textContent=frames+' frames';
  if(rec){recBuf.push((Date.now()-recT0)+' '+line)}
}
let ws;
function connect(){
  ws=new WebSocket('ws://'+location.host+'/canstream');
  ws.onopen=()=>{$('dot').classList.add('on');$('stat').innerHTML='<small>connected</small>'};
  ws.onclose=()=>{$('dot').classList.remove('on');$('stat').innerHTML='<small>reconnecting…</small>';setTimeout(connect,1500)};
  ws.onmessage=e=>handle(typeof e.data==='string'?e.data.trim():'');
}
connect();
// ---- control (HTTP, proven path) ----
async function postf(u,o){const b=new URLSearchParams(o);const r=await fetch(u,{method:'POST',body:b});return r.text()}
async function getq(u){const r=await fetch(u);return r.text()}
function key(code,hold){postf('/emulate/key',{key:code,hold:hold?1:0})}
function emu(on){getq('/api/emu?on='+(on?1:0))}
function sendRaw(){const v=$('raw').value.trim();if(v&&ws&&ws.readyState===1){ws.send(v);$('raw').value=''}}
$('raw').addEventListener('keydown',e=>{if(e.key==='Enter')sendRaw()});
// ---- record ----
function toggleRec(){rec=!rec;if(rec){recBuf=[];recT0=Date.now();recBuf.push('# MeganeCAN capture '+new Date().toISOString())}
  $('recBtn').textContent=rec?'■ Stop ('+0+')':'● Record';$('recBtn').style.color=rec?'#f38ba8':''}
setInterval(()=>{if(rec)$('recBtn').textContent='■ Stop ('+(recBuf.length-1)+')'},500);
function save(){const blob=new Blob([recBuf.join('\n')+'\n'],{type:'text/plain'});const a=document.createElement('a');
  a.href=URL.createObjectURL(blob);a.download='capture.canlog';a.click();}
</script></body></html>)WIRE";
