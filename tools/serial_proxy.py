#!/usr/bin/env python3
"""
serial_proxy.py — спільний серіал-монітор через браузер.

Читає COM-порт, пише все в serial.log (поряд з цим файлом) і стрімить
наживо в браузер через SSE. Поле вводу шле команди назад у плату.

Запуск:
    python serial_proxy.py [COM5] [115200] [8080]
або через env:
    COM_PORT=COM5 BAUD=115200 HTTP_PORT=8080 python serial_proxy.py

Браузер:  http://localhost:8080
Лог-файл: tools/serial.log  (Claude читає його)
"""
import os
import sys
import time
import queue
import threading

import serial
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_FILE = os.path.join(HERE, "serial.log")

COM_PORT = (sys.argv[1] if len(sys.argv) > 1 else os.environ.get("COM_PORT", "COM5"))
BAUD = int(sys.argv[2] if len(sys.argv) > 2 else os.environ.get("BAUD", "115200"))
HTTP_PORT = int(sys.argv[3] if len(sys.argv) > 3 else os.environ.get("HTTP_PORT", "8080"))

clients = []                 # list[queue.Queue]  — по одному на відкритий браузер
clients_lock = threading.Lock()
ser = None                   # активний serial.Serial
ser_lock = threading.Lock()


def broadcast(line: str):
    with clients_lock:
        dead = []
        for q in clients:
            try:
                q.put_nowait(line)
            except queue.Full:
                dead.append(q)
        for q in dead:
            clients.remove(q)


def log_and_broadcast(text: str):
    ts = time.strftime("%H:%M:%S")
    out = "[%s] %s" % (ts, text)
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(out + "\n")
    except Exception:
        pass
    broadcast(out)


def serial_reader():
    global ser
    buf = b""
    while True:
        try:
            with ser_lock:
                if ser is None or not ser.is_open:
                    ser = serial.Serial(COM_PORT, BAUD, timeout=1)
                    log_and_broadcast("--- [proxy] serial opened %s @ %d ---" % (COM_PORT, BAUD))
            data = ser.read(256)
            if data:
                buf += data
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    text = raw.decode("utf-8", errors="replace").rstrip("\r")
                    log_and_broadcast(text)
        except Exception as e:
            log_and_broadcast("--- [proxy] serial error: %s (retry 2s) ---" % e)
            try:
                with ser_lock:
                    if ser:
                        ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(2)


def send_command(cmd: str):
    global ser
    with ser_lock:
        if ser and ser.is_open:
            ser.write((cmd + "\n").encode("utf-8"))
            return True
    return False


PAGE = """<!doctype html><html><head><meta charset="utf-8">
<title>CTRL / MeganeCAN serial</title>
<style>
  html,body{margin:0;height:100%;background:#0b0e14;color:#cdd6f4;font:13px/1.45 Consolas,monospace}
  #bar{position:fixed;top:0;left:0;right:0;height:42px;display:flex;gap:8px;align-items:center;
       padding:0 10px;background:#11151f;border-bottom:1px solid #222a3a;z-index:2}
  #bar input{flex:1;background:#0b0e14;color:#cdd6f4;border:1px solid #2a3450;border-radius:6px;
             padding:7px 10px;font:13px Consolas,monospace}
  #bar button{background:#2a3450;color:#cdd6f4;border:0;border-radius:6px;padding:7px 12px;cursor:pointer}
  #bar button:hover{background:#3a4a6a}
  #dot{width:10px;height:10px;border-radius:50%;background:#f38ba8}
  #dot.on{background:#a6e3a1}
  #log{position:absolute;top:42px;left:0;right:0;bottom:0;overflow:auto;padding:8px 10px;white-space:pre-wrap;word-break:break-word}
  .l{padding:0 2px}
  .l:hover{background:#151a26}
  mark{background:#f9e2af;color:#11111b}
  #filter{max-width:200px;flex:0 0 180px}
  #screen{position:fixed;top:52px;right:12px;width:360px;background:#0a1f14;border:2px solid #1f6b3a;
          border-radius:10px;padding:12px 14px;z-index:3;box-shadow:0 6px 24px #000a;
          font:15px/1.5 Consolas,monospace;color:#7fffb0}
  #screen .hdr{color:#aef;font-weight:bold;border-bottom:1px solid #1f6b3a;padding-bottom:6px;margin-bottom:8px;
               display:flex;justify-content:space-between}
  #screen .it{padding:4px 8px;border-radius:5px;white-space:pre}
  #screen .it.sel{background:#1f6b3a;color:#eafff2}
  #screen .meta{margin-top:8px;font-size:11px;color:#3a7a55}
  #screen .arrow{color:#7fffb0}
</style></head><body>
<div id="screen">
  <div class="hdr"><span id="scrHdr">— display —</span><span class="arrow" id="scrArrow"></span></div>
  <div id="scrItems"></div>
  <div class="meta" id="scrMeta">waiting for @TX frames… (enable: /api/emu?on=1)</div>
</div>
<div id="bar">
  <span id="dot"></span>
  <input id="filter" placeholder="фільтр (regex)">
  <input id="cmd" placeholder="команда в плату:  cb=clearbonds  pp=play/pause  nx=next  pv=prev  e/d=display" autocomplete="off">
  <button onclick="send()">Send</button>
  <button onclick="document.getElementById('log').innerHTML=''">Clear</button>
</div>
<div id="log"></div>
<script>
const log=document.getElementById('log'), dot=document.getElementById('dot'),
      flt=document.getElementById('filter'), cmd=document.getElementById('cmd');
let re=null;
flt.oninput=()=>{ try{re=flt.value?new RegExp(flt.value,'i'):null; flt.style.color='';}catch(e){flt.style.color='#f38ba8';} };
function atBottom(){ return log.scrollHeight-log.scrollTop-log.clientHeight < 40; }
function add(text){
  if(re && !re.test(text)) return;
  const stick=atBottom();
  const d=document.createElement('div'); d.className='l';
  if(re){ d.innerHTML=text.replace(/</g,'&lt;').replace(re,m=>'<mark>'+m+'</mark>'); }
  else { d.textContent=text; }
  log.appendChild(d);
  while(log.childElementCount>4000) log.removeChild(log.firstChild);
  if(stick) log.scrollTop=log.scrollHeight;
}
// ---- AFFA3 / Carminat virtual display decoder ----
// Decodes the REAL @TX CAN frames (not debug text). ISO-TP reassembly:
// first frame byte0=0x10 carries 8 payload bytes; consecutive byte0=0x2N append
// bytes[1..7]. Menu payload (96B) layout from showMenu(): [10]=scrollLock,
// header[11..36], item1 marker[38]+text[39..63], item2 marker[65]+text[66..95].
// Highlight is a separate single frame: 07 29 01 <rowId> 80.
let asm=[], asmActive=false;
const scr={hdr:'',items:[],sel:null,scroll:0};
function asciiz(p,a,z){let s='';for(let i=a;i<=z&&i<p.length;i++){const c=p[i];if(c===0)break;s+=String.fromCharCode(c);}return s.trim();}
function decodeMenu(p){
  if(p.length<96) return false;
  scr.scroll=p[10];
  scr.hdr=asciiz(p,11,36)||'(menu)';
  scr.items=[{id:p[38],text:asciiz(p,39,63)},{id:p[65],text:asciiz(p,66,95)}];
  renderScreen(); return true;
}
function onTx(idHex,bytes){
  if(idHex!=='151') return;            // Carminat display ctrl/text
  const b0=bytes[0];
  if(b0===0x10){ asm=bytes.slice(0); asmActive=true; scr.sel=null; decodeMenu(asm); return; }
  if(asmActive && (b0&0xF0)===0x20){ asm=asm.concat(bytes.slice(1)); decodeMenu(asm); return; }
  if(b0===0x07 && bytes[1]===0x29 && bytes[2]===0x01){ scr.sel=bytes[3]; renderScreen(); return; }
}
function renderScreen(){
  document.getElementById('scrHdr').textContent=scr.hdr||'— display —';
  document.getElementById('scrArrow').textContent=
     scr.scroll===0x0B?'↓':scr.scroll===0x07?'↑':scr.scroll===0x0C?'↕':'';
  const box=document.getElementById('scrItems'); box.innerHTML='';
  scr.items.forEach(it=>{ if(!it.text&&it.id===0) return;
    const d=document.createElement('div'); d.className='it'+(it.id===scr.sel?' sel':'');
    d.textContent=(it.id===scr.sel?'▶ ':'  ')+(it.text||''); box.appendChild(d); });
  document.getElementById('scrMeta').textContent='AFFA3 0x151 · '+asm.length+'B reassembled';
}
function screenFeed(line){
  const m=line.match(/@TX\s+([0-9A-Fa-f]+)\s+(.+)$/);
  if(!m) return;
  const id=m[1].toUpperCase().replace(/^0+(?=.)/,'');
  const bytes=m[2].trim().split(/\s+/).map(h=>parseInt(h,16)).filter(n=>!isNaN(n));
  onTx(id,bytes);
}
const es=new EventSource('/stream');
es.onopen=()=>dot.classList.add('on');
es.onerror=()=>dot.classList.remove('on');
es.onmessage=e=>{ add(e.data); try{screenFeed(e.data);}catch(err){} };
function send(){
  const v=cmd.value.trim(); if(!v) return;
  fetch('/send',{method:'POST',body:v}); cmd.value='';
}
cmd.addEventListener('keydown',e=>{ if(e.key==='Enter') send(); });
</script></body></html>"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass  # тиша

    def do_GET(self):
        if self.path == "/" or self.path.startswith("/index"):
            body = PAGE.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            q = queue.Queue(maxsize=2000)
            with clients_lock:
                clients.append(q)
            try:
                self.wfile.write(b": connected\n\n")
                # програємо хвіст історії, щоб новий браузер одразу бачив контекст
                try:
                    with open(LOG_FILE, "r", encoding="utf-8", errors="replace") as f:
                        tail = f.readlines()[-300:]
                    for line in tail:
                        payload = "data: %s\n\n" % line.rstrip("\n").replace("\r", "")
                        self.wfile.write(payload.encode("utf-8"))
                except Exception:
                    pass
                self.wfile.flush()
                while True:
                    try:
                        line = q.get(timeout=15)
                        payload = "data: %s\n\n" % line.replace("\r", "")
                        self.wfile.write(payload.encode("utf-8"))
                    except queue.Empty:
                        self.wfile.write(b": ping\n\n")  # keep-alive
                    self.wfile.flush()
            except Exception:
                pass
            finally:
                with clients_lock:
                    if q in clients:
                        clients.remove(q)
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path == "/send":
            length = int(self.headers.get("Content-Length", 0))
            cmd = self.rfile.read(length).decode("utf-8", errors="replace").strip()
            ok = send_command(cmd)
            log_and_broadcast("--- [proxy] >>> %s %s ---" % (cmd, "" if ok else "(NO SERIAL)"))
            self.send_response(200 if ok else 503)
            self.end_headers()
        else:
            self.send_response(404)
            self.end_headers()


def main():
    # свіжий лог на старті
    try:
        open(LOG_FILE, "w", encoding="utf-8").close()
    except Exception:
        pass
    threading.Thread(target=serial_reader, daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler)
    print("serial %s @ %d  ->  http://localhost:%d   (log: %s)" % (COM_PORT, BAUD, HTTP_PORT, LOG_FILE))
    srv.serve_forever()


if __name__ == "__main__":
    main()
