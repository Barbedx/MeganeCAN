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
  #ctrl{position:fixed;top:52px;right:12px;width:300px;max-height:calc(100vh - 64px);overflow:auto;
        background:#0a1f14;border:2px solid #1f6b3a;border-radius:10px;padding:10px 12px;z-index:3;
        box-shadow:0 6px 24px #000a;font:12px/1.4 Consolas,monospace;color:#7fffb0}
  #ctrl .hdr{color:#aef;font-weight:bold;border-bottom:1px solid #1f6b3a;padding-bottom:6px;margin-bottom:6px}
  #ctrl .grp{border-top:1px solid #11321f;padding:7px 0 3px}
  #ctrl .grp:first-of-type{border-top:0}
  #ctrl .t{color:#5fd08a;margin-bottom:3px;font-size:11px;text-transform:uppercase;letter-spacing:.5px}
  #ctrl input,#ctrl select{width:100%;box-sizing:border-box;margin:2px 0;background:#06140d;color:#cdeedd;
        border:1px solid #1f6b3a;border-radius:5px;padding:5px 7px;font:12px Consolas,monospace}
  #ctrl button{background:#1f6b3a;color:#eafff2;border:0;border-radius:5px;padding:5px 9px;margin:4px 4px 0 0;
        cursor:pointer;font:12px Consolas,monospace}
  #ctrl button:hover{background:#2d8a4f}
  #ctrl .row{display:flex;gap:6px}
  #ctrl .row input{flex:1}
  #ctrl .note{color:#3a7a55;font-size:10px;margin-top:6px}
</style></head><body>
<div id="ctrl">
  <div class="hdr">AFFA3 control ↗</div>

  <div class="grp">
    <div class="t">Menu / now-playing</div>
    <input id="mCap" placeholder="caption / header">
    <input id="mI1" placeholder="item 1">
    <input id="mI2" placeholder="item 2">
    <select id="mScr">
      <option value="0B">arrows: ↓ down (0B)</option>
      <option value="07">arrows: ↑ up (07)</option>
      <option value="0C">arrows: ↕ both (0C)</option>
      <option value="00">arrows: none (00)</option>
    </select>
    <button onclick="fireMenu()">Show menu</button>
  </div>

  <div class="grp">
    <div class="t">Info popup (8 ch / line)</div>
    <input id="iL1" placeholder="line 1" maxlength="8">
    <input id="iL2" placeholder="line 2" maxlength="8">
    <input id="iL3" placeholder="line 3" maxlength="8">
    <button onclick="fireInfo()">Show info</button>
    <button onclick="send2('infox')">Close info</button>
  </div>

  <div class="grp">
    <div class="t">Fullscreen big text (0x21/05)</div>
    <input id="fL1" placeholder="line 1 (e.g. Please insert)">
    <input id="fL2" placeholder="line 2 (e.g. navigation CD)">
    <input id="fL3" placeholder="line 3">
    <button onclick="fireFull()">Show fullscreen</button>
    <button onclick="send2('fclose')">Close fullscreen</button>
  </div>

  <div class="grp">
    <div class="t">Confirm popup</div>
    <input id="pCap" placeholder="caption (button)">
    <input id="pR1" placeholder="row 1">
    <input id="pR2" placeholder="row 2">
    <button onclick="firePopup()">Show popup</button>
  </div>

  <div class="grp">
    <div class="t">Text · power · verbose</div>
    <input id="tTxt" placeholder="radio text line">
    <button onclick="fireTxt()">Set text</button>
    <button onclick="send2('e')">Disp ON</button>
    <button onclick="send2('d')">Disp OFF</button>
    <button onclick="send2('vb 1')">verbose on</button>
    <button onclick="send2('vb 0')">verbose off</button>
  </div>

  <div class="grp">
    <div class="t">Raw TX (protocol RE)</div>
    <div class="row">
      <input id="rxId" placeholder="id hex" style="flex:0 0 64px">
      <input id="rxB" placeholder="bytes: 10 5A 21 01 ...">
    </div>
    <button onclick="fireRaw()">Send frame</button>
    <div class="note">tx sends one raw CAN frame on the bus, then watch the
    [RX] answers in the log. Replay a multi-frame ISO-TP sequence as several
    Send-frame lines (10.., 21.., 22..). @INJ &lt;id&gt; &lt;bytes&gt; injects an RX frame.</div>
  </div>
</div>
<div id="bar">
  <span id="dot"></span>
  <input id="filter" placeholder="фільтр (regex)">
  <input id="cmd" placeholder="serial cmd:  tx 151 10 5A 21..  @INJ id bytes..  menu/info/popup/txt  vb 0|1  pp/nx/pv/cb" autocomplete="off">
  <button onclick="send()">Send</button>
  <button onclick="mark()" title="insert a labelled divider into the capture">Mark</button>
  <button onclick="location.href='/log'" title="download the raw serial.log as text">Save log</button>
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
// NOTE: this proxy no longer decodes the AFFA3 screen. There is ONE decoder — the
// firmware's ScreenDecode/twin (served at /api/screen, rendered by /preview + /wire
// on the device). Re-decoding @TX here was a second, drift-prone copy. This tool is
// now a faithful raw serial log + @TX/@RX frame view, which is what serial is best at.
const es=new EventSource('/stream');
es.onopen=()=>dot.classList.add('on');
es.onerror=()=>dot.classList.remove('on');
es.onmessage=e=>{ add(e.data); };
function send(){
  const v=cmd.value.trim(); if(!v) return;
  fetch('/send',{method:'POST',body:v}); cmd.value='';
}
cmd.addEventListener('keydown',e=>{ if(e.key==='Enter') send(); });

// --- AFFA3 control panel: build a serial command line and POST it to the board. The
// firmware SerialCommands tokenizer splits on spaces, so each text field is sent as a
// single space-free token: spaces -> '_', empty -> '~' (firmware decodeField reverses
// it). send2() fires a literal command (no encoding).
function send2(c){ fetch('/send',{method:'POST',body:c}); }
function enc(id){ const v=document.getElementById(id).value.trim().replace(/ /g,'_'); return v===''?'~':v; }
function fireMenu(){ send2('menu '+enc('mCap')+' '+enc('mI1')+' '+enc('mI2')+' '+document.getElementById('mScr').value); }
function fireInfo(){ send2('info '+enc('iL1')+' '+enc('iL2')+' '+enc('iL3')); }
function fireFull(){ send2('full '+enc('fL1')+' '+enc('fL2')+' '+enc('fL3')); }
function firePopup(){ send2('popup '+enc('pCap')+' '+enc('pR1')+' '+enc('pR2')); }
function fireTxt(){ send2('txt '+enc('tTxt')); }
function fireRaw(){
  const id=document.getElementById('rxId').value.trim(); if(!id) return;
  const b=document.getElementById('rxB').value.trim();
  send2('tx '+id+(b?' '+b:''));
}
function mark(){
  const label=prompt('mark label (e.g. "press NAV now"):','MARK');
  if(label!==null) fetch('/mark',{method:'POST',body:label});
}
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
        elif self.path == "/log" or self.path.startswith("/log?"):
            # Raw text capture — download the full serial.log as plain text. This is the
            # replacement for "save the whole HTML page": a clean log the affa_decode.py
            # tool (and humans) can read directly, no markup to strip.
            try:
                with open(LOG_FILE, "rb") as f:
                    body = f.read()
            except Exception:
                body = b""
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Disposition", "attachment; filename=affa3-capture.log")
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
        elif self.path == "/mark":
            # Insert a labelled divider into both the live view and serial.log, so a
            # capture can be split by what you were doing (e.g. "press NAV now").
            length = int(self.headers.get("Content-Length", 0))
            label = self.rfile.read(length).decode("utf-8", errors="replace").strip()
            log_and_broadcast("=================== MARK: %s ===================" % label)
            self.send_response(200)
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
