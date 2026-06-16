#pragma once

// Dead-simple end-user display preview. Open http://meganecan.local/preview on a
// phone, type text, see exactly what the panel would show — no PC tools, no serial.
//
// Flow (all on the ESP, the ONE decoder): your text -> /affa3/setMenu (the real
// showMenu encoder) -> frames -> the twin's ScreenDecode -> /api/screen -> rendered
// below. So it is the real firmware path, not a re-implementation.
//
// On load it switches the transport to VIRTUAL_ONLY so the twin ACKs and the screen
// renders with no real panel attached (a desk ESP). "Restore CAN" returns to BOTH
// for use in the car.
static const char PREVIEW_PAGE[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MeganeCAN — Display preview</title>
<style>
 body{margin:0;background:#0b0e14;color:#cdd6f4;font:15px/1.5 system-ui,sans-serif;padding:16px;max-width:560px;margin:auto}
 h2{margin:.2em 0}
 .scr{background:#0a1f14;border:2px solid #1f6b3a;border-radius:10px;padding:14px;margin:12px 0;
      font:16px/1.6 Consolas,monospace;color:#7fffb0;min-height:84px}
 .scr .hdr{color:#aef;font-weight:bold;border-bottom:1px solid #1f6b3a;padding-bottom:6px;margin-bottom:8px}
 .scr .it{padding:3px 6px;border-radius:5px;white-space:pre}
 .scr .it.sel{background:#1f6b3a;color:#eafff2}
 input{width:100%;box-sizing:border-box;background:#11151f;color:#cdd6f4;border:1px solid #2a3450;
       border-radius:8px;padding:10px;margin:4px 0;font:15px Consolas,monospace}
 button{background:#2a3450;color:#cdd6f4;border:0;border-radius:8px;padding:11px 16px;margin:4px 4px 4px 0;
        font-size:15px;cursor:pointer}
 button.go{background:#1f6b3a;color:#eafff2}
 small{color:#7a88a8}
</style></head><body>
<h2>Display preview</h2>
<small id=mode>switching to preview mode…</small>
<div class=scr id=screen><div class=hdr>— display —</div><div class=it>loading…</div></div>
<input id=h placeholder="Header (top line)" maxlength=26 value="MeganeCAN">
<input id=a placeholder="Row 1" maxlength=25 value="Hello">
<input id=b placeholder="Row 2" maxlength=30 value="from your ESP">
<div>
 <button class=go onclick=show()>Show on display</button>
 <button onclick=restore()>Restore CAN (car)</button>
</div>
<p><small>Type and press <b>Show</b>. The screen above is decoded by the ESP itself —
exactly what a real Carminat panel would render.</small></p>
<script>
const $=s=>document.getElementById(s);
const esc=s=>(s||'').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
async function getq(u){try{return await(await fetch(u)).text()}catch(e){return''}}
// Preview = no real panel: route VIRTUAL so the twin ACKs and the screen decodes.
getq('/api/route?mode=virtual').then(()=>{$('mode').textContent='preview mode (virtual — twin renders, CAN off)'});
function show(){
 const u='/affa3/setMenu?caption='+encodeURIComponent($('h').value)
        +'&name1='+encodeURIComponent($('a').value)
        +'&name2='+encodeURIComponent($('b').value);
 getq(u);
}
function restore(){getq('/api/route?mode=both').then(()=>{$('mode').textContent='CAN mode (real panel drives; preview mirrors)'})}
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
