#pragma once

// End-user / car-test display console. Open http://meganecan.local/preview (home WiFi)
// or http://192.168.4.1/preview (car AP), fire any screen the Carminat can show, and
// watch it on the real panel. The decoded box mirrors /api/screen (the ONE firmware
// decoder) for the screens it understands today (menu + highlight); info-popup and
// confirm-box are verified by looking at the REAL panel (decoder support is a TODO).
//
// On load it sets route=virtual so a desk ESP (no panel) still renders the menu box;
// in the car press "Restore CAN" (route=both) so the real panel drives + ACKs.
static const char PREVIEW_PAGE[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MeganeCAN — Display console</title>
<style>
 body{margin:0;background:#0b0e14;color:#cdd6f4;font:15px/1.5 system-ui,sans-serif;padding:14px;max-width:600px;margin:auto}
 h2{margin:.2em 0}h3{margin:.6em 0 .2em;color:#aef;font-size:14px}
 .card{background:#11151f;border:1px solid #222a3a;border-radius:10px;padding:12px;margin:10px 0}
 .scr{background:#0a1f14;border:2px solid #1f6b3a;border-radius:10px;padding:14px;
      font:16px/1.6 Consolas,monospace;color:#7fffb0;min-height:80px}
 .scr .hdr{color:#aef;font-weight:bold;border-bottom:1px solid #1f6b3a;padding-bottom:6px;margin-bottom:8px}
 .scr .it{padding:3px 6px;border-radius:5px;white-space:pre}
 .scr .it.sel{background:#1f6b3a;color:#eafff2}
 input,select{box-sizing:border-box;background:#0b0e14;color:#cdd6f4;border:1px solid #2a3450;
       border-radius:8px;padding:9px;margin:3px 0;font:14px Consolas,monospace}
 input{width:100%}
 button{background:#2a3450;color:#cdd6f4;border:0;border-radius:8px;padding:10px 14px;margin:4px 4px 0 0;
        font-size:14px;cursor:pointer}
 button.go{background:#1f6b3a;color:#eafff2}
 small{color:#7a88a8}
</style></head><body>
<h2>Display console</h2>
<small id=mode>switching to preview mode…</small>
<div class="scr" id=screen><div class=hdr>— display —</div><div class=it>loading…</div></div>
<small>↑ decoded by the ESP (menu + highlight). Info/confirm: watch the <b>real panel</b>.</small>

<div class=card>
 <h3>Menu screen</h3>
 <input id=h placeholder="Header" maxlength=26 value="MeganeCAN">
 <input id=a placeholder="Row 1" maxlength=25 value="Hello">
 <input id=b placeholder="Row 2" maxlength=30 value="from your ESP">
 <select id=sl>
  <option value="00">no arrows</option>
  <option value="0B" selected>↓ down</option>
  <option value="07">↑ up</option>
  <option value="0C">↕ both</option>
 </select>
 <div><button class=go onclick=showMenu()>Show menu</button></div>
</div>

<div class=card>
 <h3>Info popup (3 × 8 chars)</h3>
 <input id=i1 placeholder="slot 1" maxlength=8 value="AUX">
 <input id=i2 placeholder="slot 2" maxlength=8 value="AUTO">
 <input id=i3 placeholder="slot 3" maxlength=8 value="SPEED 0">
 <div><button class=go onclick=showInfo()>Show info</button><button onclick=getq('/api/info/close')>Close</button></div>
</div>

<div class=card>
 <h3>Confirm box</h3>
 <input id=cc placeholder="Caption (button)" maxlength=7 value="OK">
 <input id=c1 placeholder="Row 1" maxlength=20 value="Are you sure?">
 <input id=c2 placeholder="Row 2" maxlength=20 value="Press to confirm">
 <div><button class=go onclick=showConfirm()>Show confirm</button></div>
</div>

<div class=card>
 <h3>Display power</h3>
 <small>OFF = panel shows its own clock + outside temp (built-in sensor).</small><br>
 <button onclick=state(1)>Display ON</button>
 <button onclick=state(0)>Display OFF</button>
</div>

<div class=card>
 <h3>Transport — <span id=rtnow>?</span></h3>
 <small>both/can = drives the REAL panel. virtual = bench only (CAN TX off).</small><br>
 <button id=rt_virtual onclick=route('virtual')>virtual (desk)</button>
 <button id=rt_both onclick=route('both')>both (car)</button>
 <button id=rt_can onclick=route('can')>can only</button>
</div>

<script>
const $=s=>document.getElementById(s);
const esc=s=>(s||'').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
const enc=encodeURIComponent;
async function getq(u){try{return await(await fetch(u)).text()}catch(e){return''}}
async function post(u){try{return await(await fetch(u,{method:'POST'})).text()}catch(e){return''}}
function route(m){getq('/api/route?mode='+m).then(()=>{
  $('mode').textContent='route='+m;
  const now=$('rtnow'); if(now) now.textContent=m.toUpperCase();
  ['virtual','both','can'].forEach(x=>{const b=$('rt_'+x); if(b) b.style.outline=(x===m)?'2px solid #7fffb0':'none';});
})}
route('both'); // car-safe default: real panel is driven AND the twin decodes for the
               // preview pane. On a bench with NO real panel, click "virtual (desk)"
               // so the twin ACKs (route 'virtual' cuts CAN TX — don't auto-force it).
function showMenu(){getq('/affa3/setMenu?caption='+enc($('h').value)+'&name1='+enc($('a').value)+'&name2='+enc($('b').value)+'&scrollLock='+$('sl').value)}
function showInfo(){getq('/api/info?l1='+enc($('i1').value)+'&l2='+enc($('i2').value)+'&l3='+enc($('i3').value))}
function showConfirm(){getq('/api/confirm?caption='+enc($('cc').value)+'&row1='+enc($('c1').value)+'&row2='+enc($('c2').value))}
function state(on){post('/display/state?enable='+on)}
async function poll(){
 try{
  const s=await(await fetch('/api/screen')).json();
  const arrow=s.scroll===11?'↓':s.scroll===7?'↑':s.scroll===12?'↕':'';
  const stale=(s.screenAge_ms<0)||(s.screenAge_ms>5000);
  let h='<div class=hdr>'+esc((stale?'(stale) ':'')+arrow+' '+(s.header||'—'))+'</div>';
  [s.item0,s.item1].forEach((t,i)=>{if(t){const sel=(s.sel===126&&i===0)||(s.sel===127&&i===1);
    h+='<div class="it'+(sel?' sel':'')+'">'+esc((sel?'▶ ':'  ')+t)+'</div>'}});
  $('screen').innerHTML=h;
 }catch(e){}
}
setInterval(poll,600);poll();
</script></body></html>)HTML";
