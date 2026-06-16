#pragma once
#include <Arduino.h>

// /diag PID-scan page (extracted from HttpServerManager.cpp).
static const char DIAG_PAGE[] PROGMEM = R"rawliteral(
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
