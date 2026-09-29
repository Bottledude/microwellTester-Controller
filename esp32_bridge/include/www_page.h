#pragma once
/* Interactive coil-control webpage served by the ESP32 SoftAP (tabbed UI). */

static const char WWW_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Coil Control</title>
<style>
  :root {
    --bg: #0e1418;
    --panel: #172026;
    --line: #2a3a44;
    --text: #e6eef2;
    --muted: #8aa0ab;
    --accent: #2ec4b6;
    --accent-dim: #1a7a72;
    --warn: #e9a825;
    --off: #3a4a52;
    --fwd: #3dbe7a;
    --rev: #e07a5f;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; font-family: "IBM Plex Sans", "Segoe UI", system-ui, sans-serif;
    background:
      radial-gradient(1200px 600px at 10% -10%, #1a3038 0%, transparent 55%),
      radial-gradient(900px 500px at 100% 0%, #1c2430 0%, transparent 50%),
      var(--bg);
    color: var(--text); min-height: 100vh;
  }
  header {
    display: flex; flex-wrap: wrap; gap: 12px; align-items: center;
    justify-content: space-between;
    padding: 18px 22px; border-bottom: 1px solid var(--line);
  }
  header h1 {
    margin: 0; font-size: 1.25rem; font-weight: 600; letter-spacing: 0.02em;
  }
  header h1 span { color: var(--accent); font-weight: 500; }
  .meta { color: var(--muted); font-size: 0.85rem; }
  .pill {
    display: inline-flex; align-items: center; gap: 6px;
    padding: 4px 10px; border-radius: 999px; border: 1px solid var(--line);
    background: var(--panel); font-size: 0.8rem;
  }
  .dot { width: 8px; height: 8px; border-radius: 50%; background: var(--warn); }
  .dot.ok { background: var(--accent); box-shadow: 0 0 8px var(--accent-dim); }
  .tabs {
    display: flex; gap: 4px; padding: 12px 22px 0; max-width: 1100px; margin: 0 auto;
    border-bottom: 1px solid var(--line);
  }
  .tabs button {
    appearance: none; border: 1px solid transparent; border-bottom: none;
    background: transparent; color: var(--muted); padding: 10px 16px;
    border-radius: 8px 8px 0 0; cursor: pointer; font: inherit; font-size: 0.92rem;
  }
  .tabs button:hover { color: var(--text); }
  .tabs button.active {
    color: var(--text); background: var(--panel); border-color: var(--line);
    border-bottom-color: var(--panel); margin-bottom: -1px;
  }
  main { padding: 18px 22px 40px; max-width: 1100px; margin: 0 auto; }
  .tab-panel { display: none; }
  .tab-panel.active { display: block; }
  .toolbar {
    display: flex; flex-wrap: wrap; gap: 10px; margin-bottom: 18px;
  }
  button, .btn {
    appearance: none; border: 1px solid var(--line); background: var(--panel);
    color: var(--text); padding: 10px 14px; border-radius: 8px; cursor: pointer;
    font: inherit; font-size: 0.9rem;
  }
  button:hover { border-color: var(--accent); }
  button.primary { background: var(--accent-dim); border-color: var(--accent); color: #fff; }
  button.danger { border-color: var(--rev); color: #ffd5cb; }
  .grid {
    display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
    gap: 14px;
  }
  .card {
    background: var(--panel); border: 1px solid var(--line); border-radius: 12px;
    padding: 14px 14px 16px;
  }
  .card.live { border-color: var(--accent-dim); }
  .card-head {
    display: flex; justify-content: space-between; align-items: center;
    margin-bottom: 12px;
  }
  .card-head h2 { margin: 0; font-size: 1.05rem; }
  .section-title {
    margin: 0 0 6px; font-size: 1.05rem; font-weight: 600;
  }
  .section-help { margin: 0 0 14px; color: var(--muted); font-size: 0.85rem; }
  .cfg-grid {
    display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
    gap: 14px;
  }
  .field { margin: 10px 0; }
  .field label {
    display: block; color: var(--muted); font-size: 0.8rem; margin-bottom: 6px;
  }
  .field select, .field input[type=number], .field input[type=text] {
    width: 100%; padding: 9px 10px; border-radius: 8px; border: 1px solid var(--line);
    background: #0a1014; color: var(--text); font: inherit;
  }
  .seg {
    display: grid; grid-template-columns: 1fr 1fr; gap: 8px;
  }
  .seg button.active { border-color: var(--accent); background: var(--accent-dim); color: #fff; }
  .switch {
    position: relative; width: 46px; height: 26px;
  }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider {
    position: absolute; inset: 0; background: var(--off); border-radius: 999px;
    cursor: pointer; transition: .15s;
  }
  .slider:before {
    content: ""; position: absolute; width: 20px; height: 20px; left: 3px; top: 3px;
    background: #fff; border-radius: 50%; transition: .15s;
  }
  .switch input:checked + .slider { background: var(--accent-dim); }
  .switch input:checked + .slider:before { transform: translateX(20px); }
  label.row {
    display: flex; justify-content: space-between; align-items: center;
    margin: 10px 0 4px; color: var(--muted); font-size: 0.8rem;
  }
  input[type=range] { width: 100%; accent-color: var(--accent); }
  .dir {
    display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 8px;
  }
  .dir button.active.fwd { background: #1e4d36; border-color: var(--fwd); }
  .dir button.active.rev { background: #5a3228; border-color: var(--rev); }
  .readout {
    margin-top: 12px; padding-top: 10px; border-top: 1px dashed var(--line);
    display: grid; grid-template-columns: 1fr 1fr; gap: 6px;
    font-size: 0.82rem; color: var(--muted);
  }
  .readout b { color: var(--text); font-weight: 560; }
  .log-toolbar { display: flex; flex-wrap: wrap; gap: 10px; margin-bottom: 12px; align-items: center; }
  .log-toolbar input[type=text] {
    flex: 1; min-width: 180px; padding: 9px 10px; border-radius: 8px;
    border: 1px solid var(--line); background: #0a1014; color: var(--text); font: inherit;
  }
  .log {
    background: #0a1014; border: 1px solid var(--line);
    border-radius: 10px; padding: 12px; min-height: 320px;
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    font-size: 0.78rem; color: var(--muted); white-space: pre-wrap;
    max-height: 60vh; overflow: auto;
  }
  .kv { font-size: 0.85rem; color: var(--muted); line-height: 1.55; }
  .kv b { color: var(--text); font-weight: 560; }
</style>
</head>
<body>
<header>
  <div>
    <h1>Coil Control <span>· H743 / BTS7960</span></h1>
    <div class="meta">WiFi bridge · serial CLI remains available on USART1</div>
  </div>
  <div class="pill"><span class="dot" id="linkDot"></span><span id="linkText">connecting…</span></div>
</header>

<nav class="tabs" role="tablist">
  <button type="button" class="active" data-tab="config" onclick="showTab('config')">Configuration</button>
  <button type="button" data-tab="motors" onclick="showTab('motors')">Motor drivers</button>
  <button type="button" data-tab="logs" onclick="showTab('logs')">Logs</button>
</nav>

<main>
  <!-- ========== Configuration ========== -->
  <section id="tab-config" class="tab-panel active">
    <h2 class="section-title">System configuration</h2>
    <p class="section-help">Set control mode, soft current limits, and program presets before driving coils.</p>
    <div class="cfg-grid">
      <div class="card">
        <h2>Control mode</h2>
        <p class="section-help">Open-loop uses PWM duty 0–255. Closed-loop uses IS feedback toward I_ref.</p>
        <div class="seg">
          <button type="button" id="modeOpenBtn" class="active" onclick="setMode('open')">Open-loop</button>
          <button type="button" id="modeClosedBtn" onclick="setMode('closed')">Closed-loop</button>
        </div>
        <div class="kv" style="margin-top:12px">Active: <b id="cfgModeLabel">open</b></div>
      </div>

      <div class="card">
        <h2>Soft current limit</h2>
        <p class="section-help">0–255 maps to ~0–2 A (255 ≈ 2 A). Applied to all coils or one channel.</p>
        <div class="field">
          <label for="limTarget">Target</label>
          <select id="limTarget">
            <option value="all">All coils</option>
            <option value="M1">M1</option><option value="M2">M2</option>
            <option value="M3">M3</option><option value="M4">M4</option>
            <option value="M5">M5</option><option value="M6">M6</option>
          </select>
        </div>
        <div class="field">
          <label for="limVal">Limit (0–255)</label>
          <input id="limVal" type="number" min="0" max="255" value="255"/>
        </div>
        <button type="button" class="primary" onclick="applyLimit()">Apply limit</button>
      </div>

      <div class="card">
        <h2>Default magnitude</h2>
        <p class="section-help">Sets magnitude on all coils (does not enable them).</p>
        <div class="field">
          <label for="defMag">Magnitude (0–255)</label>
          <input id="defMag" type="number" min="0" max="255" value="128"/>
        </div>
        <button type="button" onclick="applyDefaultMag()">Set all magnitudes</button>
      </div>

      <div class="card">
        <h2>Programs &amp; contour</h2>
        <p class="section-help">Presets for multi-coil field patterns (M1–M4).</p>
        <div class="toolbar" style="margin:0">
          <button type="button" class="primary" onclick="sendCmd('contour on')">Contour ON</button>
          <button type="button" onclick="sendCmd('contour off')">Contour OFF</button>
          <button type="button" onclick="sendCmd('program P1')">Program P1</button>
          <button type="button" onclick="sendCmd('program P2')">Program P2</button>
          <button type="button" class="danger" onclick="sendCmd('off all')">ALL OFF</button>
        </div>
        <div class="kv" style="margin-top:12px">Contour: <b id="cfgContourLabel">off</b></div>
      </div>

      <div class="card">
        <h2>Link &amp; wiring</h2>
        <div class="kv">
          SoftAP: <b>CoilControl</b> / <b>coilcontrol</b><br/>
          UI: <b>http://192.168.4.1/</b><br/>
          ESP32 TX GPIO17 → STM32 PD9 (RX)<br/>
          ESP32 RX GPIO18 ← STM32 PD8 (TX)<br/>
          Serial Monitor: USART1 PA9/PA10 @ 115200
        </div>
        <div class="toolbar" style="margin-top:12px;margin-bottom:0">
          <button type="button" onclick="poll()">Refresh status</button>
        </div>
      </div>
    </div>
  </section>

  <!-- ========== Motor drivers ========== -->
  <section id="tab-motors" class="tab-panel">
    <h2 class="section-title">Motor drivers</h2>
    <p class="section-help">Per-coil enable, magnitude, and B-field direction. Quick actions mirror Configuration programs.</p>
    <div class="toolbar">
      <button type="button" class="primary" onclick="sendCmd('contour on')">Contour ON (M1–M4)</button>
      <button type="button" onclick="sendCmd('contour off')">Contour OFF</button>
      <button type="button" onclick="sendCmd('program P1')">Program P1</button>
      <button type="button" onclick="sendCmd('program P2')">Program P2</button>
      <button type="button" class="danger" onclick="sendCmd('off all')">ALL OFF</button>
    </div>
    <div class="grid" id="coils"></div>
  </section>

  <!-- ========== Logs ========== -->
  <section id="tab-logs" class="tab-panel">
    <h2 class="section-title">Logs</h2>
    <p class="section-help">Command traffic and bridge replies. You can also type a raw STM32 CLI line.</p>
    <div class="log-toolbar">
      <input id="rawCmd" type="text" placeholder="e.g. drive M1 180 +   or   status" onkeydown="if(event.key==='Enter')sendRawCmd()"/>
      <button type="button" class="primary" onclick="sendRawCmd()">Send</button>
      <button type="button" onclick="clearLog()">Clear</button>
    </div>
    <div class="log" id="log">[ready] join SoftAP and wait for linked status…</div>
  </section>
</main>

<script>
const N = 6;
let state = { mode: 'open', contour: 0, coils: [] };
for (let i = 1; i <= N; i++) {
  state.coils.push({ id: i, en: 0, mag: 0, dir: 1, duty: 0, i: 0, lim: 2 });
}

function showTab(name) {
  document.querySelectorAll('.tab-panel').forEach(p => p.classList.toggle('active', p.id === 'tab-' + name));
  document.querySelectorAll('.tabs button').forEach(b => b.classList.toggle('active', b.dataset.tab === name));
}

function log(msg) {
  const el = document.getElementById('log');
  const t = new Date().toLocaleTimeString();
  el.textContent = `[${t}] ${msg}\n` + el.textContent.slice(0, 8000);
}

function clearLog() {
  document.getElementById('log').textContent = '';
}

async function api(path, body) {
  const opts = body ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) } : {};
  const r = await fetch(path, opts);
  const text = await r.text();
  let j = {};
  try { j = JSON.parse(text); } catch (_) { j = { raw: text }; }
  // #region agent log
  try {
    const dbg = j._dbg || [];
    for (const line of dbg) {
      let payload = null;
      try { payload = (typeof line === 'string') ? JSON.parse(line) : line; } catch (_) {}
      if (payload) {
        fetch('http://127.0.0.1:7447/ingest/240151b5-f208-49ac-8799-638beadd3d98',{method:'POST',headers:{'Content-Type':'application/json','X-Debug-Session-Id':'07440e'},body:JSON.stringify(Object.assign({sessionId:'07440e',runId:'post-fix',timestamp:Date.now()}, payload))}).catch(()=>{});
      }
    }
  } catch (_) {}
  // #endregion
  if (!r.ok) throw new Error((j.raw && String(j.raw).slice(0, 80)) || ('HTTP ' + r.status));
  return j;
}

async function sendCmd(cmd) {
  try {
    const j = await api('/api/cmd', { cmd });
    log('> ' + cmd + (j.raw ? '  ← ' + String(j.raw).trim().slice(0, 120) : ''));
    if (j.status) applyStatus(j.status);
    if (j.ok !== false) setLink(true);
    else setLink(false);
    syncConfigLabels();
  } catch (e) { log('ERR ' + e.message); setLink(false); }
}

async function sendRawCmd() {
  const el = document.getElementById('rawCmd');
  const cmd = (el.value || '').trim();
  if (!cmd) return;
  el.value = '';
  await sendCmd(cmd);
}

async function setMode(m) {
  await sendCmd('mode ' + m);
  syncConfigLabels();
}

async function applyLimit() {
  const t = document.getElementById('limTarget').value;
  let v = +document.getElementById('limVal').value;
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  await sendCmd('limit ' + t + ' ' + v);
}

async function applyDefaultMag() {
  let v = +document.getElementById('defMag').value;
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  await sendCmd('set all ' + v);
}

async function applyCoil(i) {
  const c = state.coils[i];
  const dir = c.dir >= 0 ? '+' : '-';
  if (c.en) await sendCmd(`drive M${c.id} ${c.mag} ${dir}`);
  else await sendCmd(`off M${c.id}`);
}

function syncConfigLabels() {
  document.getElementById('cfgModeLabel').textContent = state.mode;
  document.getElementById('cfgContourLabel').textContent = state.contour ? 'ON' : 'off';
  document.getElementById('modeOpenBtn').classList.toggle('active', state.mode === 'open');
  document.getElementById('modeClosedBtn').classList.toggle('active', state.mode === 'closed');
}

function applyStatus(s) {
  if (!s || !s.coils) return;
  state.mode = s.mode || state.mode;
  state.contour = s.contour || 0;
  s.coils.forEach((c, idx) => {
    if (!state.coils[idx]) return;
    Object.assign(state.coils[idx], c);
  });
  render(false);
  syncConfigLabels();
  setLink(true);
}

function setLink(ok) {
  document.getElementById('linkDot').className = 'dot' + (ok ? ' ok' : '');
  document.getElementById('linkText').textContent = ok
    ? `linked · mode ${state.mode} · contour ${state.contour ? 'ON' : 'off'}`
    : 'STM32 link lost';
}

function render(rebuild) {
  const root = document.getElementById('coils');
  if (rebuild || !root.children.length) {
    root.innerHTML = '';
    state.coils.forEach((c, idx) => {
      const d = document.createElement('div');
      d.className = 'card' + (c.en ? ' live' : '');
      d.id = 'card' + idx;
      d.innerHTML = `
        <div class="card-head">
          <h2>Coil M${c.id}</h2>
          <label class="switch" title="Enable">
            <input type="checkbox" ${c.en ? 'checked' : ''} onchange="toggleEn(${idx}, this.checked)">
            <span class="slider"></span>
          </label>
        </div>
        <label class="row"><span>Magnitude</span><b id="magv${idx}">${c.mag}</b></label>
        <input type="range" min="0" max="255" value="${c.mag}"
          oninput="document.getElementById('magv${idx}').textContent=this.value"
          onchange="setMag(${idx}, +this.value)">
        <label class="row"><span>Direction (B-field)</span></label>
        <div class="dir">
          <button type="button" class="fwd ${c.dir>=0?'active':''}" onclick="setDir(${idx},1)">Forward +</button>
          <button type="button" class="rev ${c.dir<0?'active':''}" onclick="setDir(${idx},-1)">Reverse −</button>
        </div>
        <div class="readout">
          <div>Duty <b id="duty${idx}">${c.duty}</b></div>
          <div>Current <b id="amps${idx}">${(+c.i).toFixed(3)} A</b></div>
          <div>Limit <b id="limr${idx}">${(+c.lim).toFixed(2)} A</b></div>
          <div>State <b id="sten${idx}">${c.en?'ON':'OFF'}</b></div>
        </div>`;
      root.appendChild(d);
    });
  } else {
    state.coils.forEach((c, idx) => {
      const card = document.getElementById('card' + idx);
      if (!card) return;
      card.className = 'card' + (c.en ? ' live' : '');
      const en = card.querySelector('input[type=checkbox]');
      if (en) en.checked = !!c.en;
      const range = card.querySelector('input[type=range]');
      if (range && document.activeElement !== range) range.value = c.mag;
      const magv = document.getElementById('magv' + idx);
      if (magv && document.activeElement !== range) magv.textContent = c.mag;
      document.getElementById('duty' + idx).textContent = c.duty;
      document.getElementById('amps' + idx).textContent = (+c.i).toFixed(3) + ' A';
      const limr = document.getElementById('limr' + idx);
      if (limr) limr.textContent = (+c.lim).toFixed(2) + ' A';
      document.getElementById('sten' + idx).textContent = c.en ? 'ON' : 'OFF';
      const buttons = card.querySelectorAll('.dir button');
      buttons[0].className = 'fwd' + (c.dir >= 0 ? ' active' : '');
      buttons[1].className = 'rev' + (c.dir < 0 ? ' active' : '');
    });
  }
}

async function toggleEn(i, on) {
  state.coils[i].en = on ? 1 : 0;
  await applyCoil(i);
}
async function setMag(i, v) {
  state.coils[i].mag = v;
  if (state.coils[i].en) await applyCoil(i);
  else await sendCmd(`set M${state.coils[i].id} ${v}`);
}
async function setDir(i, d) {
  state.coils[i].dir = d;
  if (state.coils[i].en) await applyCoil(i);
  else await sendCmd(`dir M${state.coils[i].id} ${d>=0?'+':'-'}`);
  render(false);
}

let _pollBusy = false;
async function poll() {
  if (_pollBusy) return;
  _pollBusy = true;
  try {
    const j = await api('/api/status');
    if (j.status) applyStatus(j.status);
    setLink(!!(j.ok || j.linked));
    syncConfigLabels();
  } catch (e) {
    setLink(false);
  } finally {
    _pollBusy = false;
  }
}

showTab('config');
render(true);
syncConfigLabels();
poll();
setInterval(poll, 1200);
</script>
</body>
</html>
)HTML";
