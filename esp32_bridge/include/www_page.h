#pragma once
/* Lab instrument webpage served by the ESP32 SoftAP. */

static const char WWW_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>SMC Field Controller</title>
<style>
  :root {
    --bg: #12161a;
    --panel: #1a2128;
    --panel2: #151b21;
    --line: #2c3842;
    --text: #e8eef2;
    --muted: #8b9aa6;
    --accent: #3aa6a0;
    --accent-dim: #246e6a;
    --warn: #d4a017;
    --off: #3a4650;
    --fwd: #3dbe7a;
    --rev: #d76a52;
    --danger: #c44b3a;
    --mono: "IBM Plex Mono", "SF Mono", ui-monospace, Menlo, monospace;
    --sans: "IBM Plex Sans", "Segoe UI", system-ui, sans-serif;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: var(--sans);
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
  }
  .topbar {
    position: sticky; top: 0; z-index: 20;
    display: flex; flex-wrap: wrap; gap: 10px; align-items: center;
    justify-content: space-between;
    padding: 12px 16px;
    background: var(--panel);
    border-bottom: 1px solid var(--line);
  }
  .brand h1 {
    margin: 0; font-size: 1.05rem; font-weight: 600; letter-spacing: 0.01em;
  }
  .brand .sub { color: var(--muted); font-size: 0.78rem; margin-top: 2px; }
  .top-actions { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
  .pill {
    display: inline-flex; align-items: center; gap: 6px;
    padding: 5px 10px; border-radius: 6px; border: 1px solid var(--line);
    background: var(--panel2); font-size: 0.78rem; font-family: var(--mono);
  }
  .dot { width: 8px; height: 8px; border-radius: 50%; background: var(--warn); flex-shrink: 0; }
  .dot.ok { background: var(--accent); }
  .status-strip {
    display: flex; flex-wrap: wrap; gap: 14px;
    padding: 8px 16px;
    border-bottom: 1px solid var(--line);
    background: var(--panel2);
    font-family: var(--mono); font-size: 0.78rem; color: var(--muted);
  }
  .status-strip b { color: var(--text); font-weight: 560; }
  .banner {
    display: none; margin: 12px 16px 0; padding: 10px 12px;
    border: 1px solid var(--warn); background: #2a2410; color: #f0e0a0;
    border-radius: 6px; font-size: 0.85rem;
  }
  .banner.show { display: block; }
  .banner.err { border-color: var(--danger); background: #2a1512; color: #ffcfc6; }
  .tabs {
    display: flex; gap: 2px; padding: 10px 16px 0; max-width: 1100px; margin: 0 auto;
    border-bottom: 1px solid var(--line);
  }
  .tabs button {
    appearance: none; border: 1px solid transparent; border-bottom: none;
    background: transparent; color: var(--muted); padding: 10px 14px;
    border-radius: 6px 6px 0 0; cursor: pointer; font: inherit; font-size: 0.9rem;
  }
  .tabs button:hover { color: var(--text); }
  .tabs button.active {
    color: var(--text); background: var(--panel); border-color: var(--line);
    border-bottom-color: var(--panel); margin-bottom: -1px;
  }
  main { padding: 16px; max-width: 1100px; margin: 0 auto; }
  .tab-panel { display: none; }
  .tab-panel.active { display: block; }
  .section-title { margin: 0 0 4px; font-size: 1.05rem; font-weight: 600; }
  .section-help { margin: 0 0 14px; color: var(--muted); font-size: 0.84rem; }
  .cfg-grid {
    display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
    gap: 12px;
  }
  .grid {
    display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
    gap: 12px;
  }
  .card {
    background: var(--panel); border: 1px solid var(--line); border-radius: 8px;
    padding: 14px;
  }
  .card.live { border-color: var(--accent-dim); }
  .card.disabled { opacity: 0.45; pointer-events: none; }
  .card h2 { margin: 0 0 8px; font-size: 0.98rem; }
  .card-head {
    display: flex; justify-content: space-between; align-items: center;
    margin-bottom: 10px;
  }
  .card-head h2 { margin: 0; }
  button, .btn {
    appearance: none; border: 1px solid var(--line); background: var(--panel2);
    color: var(--text); padding: 10px 14px; border-radius: 6px; cursor: pointer;
    font: inherit; font-size: 0.9rem; min-height: 40px;
  }
  button:hover { border-color: var(--accent); }
  button:disabled { opacity: 0.4; cursor: not-allowed; }
  button.primary { background: var(--accent-dim); border-color: var(--accent); color: #fff; }
  button.danger { border-color: var(--danger); color: #ffd5cb; background: #2a1815; }
  button.danger:hover { border-color: #f08070; }
  button.large { min-height: 48px; font-size: 1rem; font-weight: 600; padding: 12px 18px; }
  .toolbar { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 14px; }
  .field { margin: 10px 0; }
  .field label {
    display: flex; justify-content: space-between; align-items: baseline;
    color: var(--muted); font-size: 0.78rem; margin-bottom: 6px;
  }
  .field label b { color: var(--text); font-family: var(--mono); font-size: 0.9rem; }
  .field select, .field input[type=number], .field input[type=text] {
    width: 100%; padding: 10px; border-radius: 6px; border: 1px solid var(--line);
    background: #0e1317; color: var(--text); font: inherit; font-family: var(--mono);
  }
  input[type=range] { width: 100%; accent-color: var(--accent); }
  .seg { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
  .seg.triple { grid-template-columns: 1fr 1fr 1fr; }
  .seg button.active { border-color: var(--accent); background: var(--accent-dim); color: #fff; }
  .switch { position: relative; width: 46px; height: 26px; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider {
    position: absolute; inset: 0; background: var(--off); border-radius: 999px;
    cursor: pointer; transition: .12s;
  }
  .slider:before {
    content: ""; position: absolute; width: 20px; height: 20px; left: 3px; top: 3px;
    background: #fff; border-radius: 50%; transition: .12s;
  }
  .switch input:checked + .slider { background: var(--accent-dim); }
  .switch input:checked + .slider:before { transform: translateX(20px); }
  label.row {
    display: flex; justify-content: space-between; align-items: center;
    margin: 10px 0 4px; color: var(--muted); font-size: 0.78rem;
  }
  .dir { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 8px; }
  .dir button.active.fwd { background: #1e4d36; border-color: var(--fwd); }
  .dir button.active.rev { background: #5a3228; border-color: var(--rev); }
  .readout {
    margin-top: 12px; padding-top: 10px; border-top: 1px solid var(--line);
    display: grid; grid-template-columns: 1fr 1fr; gap: 6px;
    font-size: 0.8rem; color: var(--muted); font-family: var(--mono);
  }
  .readout b { color: var(--text); font-weight: 560; }
  .kv { font-size: 0.84rem; color: var(--muted); line-height: 1.55; }
  .kv b { color: var(--text); font-weight: 560; font-family: var(--mono); }
  .phase-map {
    display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 10px;
  }
  .phase-box {
    border: 1px solid var(--line); border-radius: 6px; padding: 10px;
    background: var(--panel2);
  }
  .phase-box .t { font-size: 0.75rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.04em; }
  .phase-box .v { font-family: var(--mono); font-size: 0.95rem; margin-top: 4px; }
  .log-toolbar { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 10px; align-items: center; }
  .log-toolbar input[type=text] {
    flex: 1; min-width: 180px; padding: 10px; border-radius: 6px;
    border: 1px solid var(--line); background: #0e1317; color: var(--text);
    font: inherit; font-family: var(--mono);
  }
  .log {
    background: #0e1317; border: 1px solid var(--line);
    border-radius: 8px; padding: 12px; min-height: 320px;
    font-family: var(--mono); font-size: 0.76rem; color: var(--muted);
    white-space: pre-wrap; max-height: 60vh; overflow: auto;
  }
  /* Teleplot-style waveform panel */
  .plot-card { margin-bottom: 14px; padding: 12px; }
  .plot-head {
    display: flex; flex-wrap: wrap; gap: 10px; align-items: center;
    justify-content: space-between; margin-bottom: 8px;
  }
  .plot-head h2 { margin: 0; font-size: 0.95rem; }
  .plot-legend {
    display: flex; flex-wrap: wrap; gap: 6px 12px; align-items: center;
    font-family: var(--mono); font-size: 0.78rem;
  }
  .plot-legend label {
    display: inline-flex; align-items: center; gap: 6px;
    cursor: pointer; user-select: none; color: var(--muted);
    padding: 2px 6px; border-radius: 4px; border: 1px solid transparent;
  }
  .plot-legend label.on { color: var(--text); border-color: var(--line); background: var(--panel2); }
  .plot-legend label.off { opacity: 0.4; text-decoration: line-through; }
  .plot-legend .swatch {
    width: 12px; height: 3px; border-radius: 1px; display: inline-block;
  }
  .plot-legend input { accent-color: var(--accent); margin: 0; }
  .plot-wrap {
    position: relative; width: 100%; height: 220px;
    background: #0b0f13; border: 1px solid var(--line); border-radius: 6px;
    overflow: hidden;
  }
  #waveCanvas { display: block; width: 100%; height: 100%; }
  .plot-meta {
    display: flex; flex-wrap: wrap; gap: 12px; margin-top: 8px;
    font-family: var(--mono); font-size: 0.72rem; color: var(--muted);
  }
  .plot-meta b { color: var(--text); font-weight: 560; }
</style>
</head>
<body>
<div class="topbar">
  <div class="brand">
    <h1>SMC Field Controller</h1>
    <div class="sub">STM32H743 · BTS7960 · lab instrument UI</div>
  </div>
  <div class="top-actions">
    <div class="pill"><span class="dot" id="linkDot"></span><span id="linkText">connecting…</span></div>
    <button type="button" class="danger large" onclick="allOff()">ALL OFF</button>
  </div>
</div>

<div class="status-strip" id="statusStrip">
  <span>rotate <b id="stRot">off</b></span>
  <span>axis <b id="stAxis">z</b></span>
  <span>freq <b id="stFreq">1.00</b> Hz</span>
  <span>amp <b id="stAmp">0</b></span>
  <span>dir <b id="stDir">cw</b></span>
  <span>mode <b id="stMode">open</b></span>
  <span>I<sub>A</sub> <b id="stIA">—</b></span>
  <span>I<sub>B</sub> <b id="stIB">—</b></span>
</div>
<div class="banner" id="msgBanner"></div>

<nav class="tabs" role="tablist">
  <button type="button" class="active" data-tab="field" onclick="showTab('field')">Field</button>
  <button type="button" data-tab="dc" onclick="showTab('dc')">DC coils</button>
  <button type="button" data-tab="logs" onclick="showTab('logs')">Logs</button>
</nav>

<main>
  <section id="tab-field" class="tab-panel active">
    <div class="card plot-card">
      <div class="plot-head">
        <h2>Live coil waveforms</h2>
        <div class="plot-legend" id="plotLegend"></div>
      </div>
      <div class="plot-wrap"><canvas id="waveCanvas"></canvas></div>
      <div class="plot-meta">
        <span>window <b id="plotWin">2.0</b> s</span>
        <span>y: duty <b>±255</b></span>
        <span>source <b>commanded open-loop</b></span>
        <span id="plotRunHint">rotate off → flat</span>
      </div>
    </div>
    <h2 class="section-title">Rotating magnetic field</h2>
    <p class="section-help">Four coils at 90°, each tilted ~45°. <b>Z</b>: in-plane quadrature on all four. <b>X</b>: M2∥M4 same current direction so horizontal fields cancel and vertical fields add — vary that current for torque about X (M1–M3 axis). Amp ramps on start/stop/dir/axis (~1.3 s). ALL OFF still cuts immediately.</p>
    <div class="cfg-grid">
      <div class="card">
        <h2>Run control</h2>
        <div class="toolbar" style="margin:0">
          <button type="button" class="primary large" id="btnRotOn" onclick="setRotate(true)">START</button>
          <button type="button" class="danger large" id="btnRotOff" onclick="setRotate(false)">STOP</button>
        </div>
        <div class="kv" style="margin-top:12px">State: <b id="cfgRotLabel">off</b> · axis <b id="cfgAxisLabel">Z</b></div>
        <div class="phase-map">
          <div class="phase-box">
            <div class="t" id="phaseATitle">M1 ∥ M3 · X</div>
            <div class="v" id="phaseARole">Z: Phase A @ 0°</div>
            <div class="kv" style="margin-top:6px">I ≈ <b id="phaseAI">0.000</b> A</div>
          </div>
          <div class="phase-box">
            <div class="t" id="phaseBTitle">M2 ∥ M4 · Y</div>
            <div class="v" id="phaseBRole">Z: Phase B @ ±90°</div>
            <div class="kv" style="margin-top:6px">I ≈ <b id="phaseBI">0.000</b> A</div>
          </div>
        </div>
      </div>

      <div class="card">
        <h2>Rotation axis</h2>
        <p class="section-help">Z = spin in the coil plane. X = M2 and M4 in-phase (net vertical field).</p>
        <div class="seg">
          <button type="button" id="axisZBtn" class="active" onclick="setAxis('z')">Z (in-plane)</button>
          <button type="button" id="axisXBtn" onclick="setAxis('x')">X (M2∥M4)</button>
        </div>
      </div>

      <div class="card">
        <h2>Frequency</h2>
        <p class="section-help">Excitation 0.1–50 Hz. Carrier PWM stays ~40 kHz.</p>
        <div class="field">
          <label for="freqVal">Frequency (Hz) <b id="freqLabel">1.00</b></label>
          <input id="freqVal" type="number" min="0.1" max="50" step="0.1" value="1.0"
            oninput="onFreqInput(+this.value)"
            onchange="applyFreq()"/>
        </div>
        <input type="range" id="freqRange" min="1" max="500" value="10"
          oninput="syncFreqFromRange(this.value)"
          onchange="applyFreq()"/>
        <div class="kv" style="margin-top:8px">Slider = 0.1–50.0 Hz (×0.1)</div>
      </div>

      <div class="card">
        <h2>Amplitude</h2>
        <p class="section-help">Shared peak duty 0–255. Z uses all four coils; X uses M2∥M4 only.</p>
        <div class="field">
          <label for="ampVal">Amplitude <b id="ampLabel">0</b></label>
          <input id="ampVal" type="number" min="0" max="255" step="1" value="0"
            oninput="onAmpInput(+this.value)"
            onchange="applyAmp()"/>
        </div>
        <input type="range" id="ampRange" min="0" max="255" value="0"
          oninput="onAmpInput(+this.value)"
          onchange="applyAmp()"/>
      </div>

      <div class="card">
        <h2>Rotation direction</h2>
        <p class="section-help">Z: CW = Phase B +90°, CCW = −90°. X: inverts the common M2∥M4 sine.</p>
        <div class="seg">
          <button type="button" id="dirCwBtn" class="active" onclick="setRotDir('cw')">CW</button>
          <button type="button" id="dirCcwBtn" onclick="setRotDir('ccw')">CCW</button>
        </div>
      </div>

      <div class="card">
        <h2>Soft current limit</h2>
        <p class="section-help">0–255 ≈ 0–2 A. Applied to all coils. While rotating, over-limit reduces live amplitude.</p>
        <div class="field">
          <label for="limVal">Limit (0–255)</label>
          <input id="limVal" type="number" min="0" max="255" value="255"/>
        </div>
        <button type="button" class="primary" onclick="applyLimit()">Apply limit (all)</button>
      </div>

      <div class="card">
        <h2>Link &amp; wiring</h2>
        <div class="kv">
          SoftAP: <b>CoilControl</b> / <b>coilcontrol</b><br/>
          UI: <b>http://192.168.4.1/</b><br/>
          Serial CLI: USART1 @ 115200<br/>
          ESP32 TX17 → PD9 · RX18 ← PD8
        </div>
        <div class="toolbar" style="margin-top:12px;margin-bottom:0">
          <button type="button" onclick="poll()">Refresh status</button>
        </div>
      </div>
    </div>
  </section>

  <section id="tab-dc" class="tab-panel">
    <h2 class="section-title">DC coil drivers</h2>
    <p class="section-help">Constant magnitude / direction (not sine). All four coils lock while a rotating field is active.</p>
    <div class="banner show" id="dcLockBanner" style="display:none;margin:0 0 12px">Rotating field active — stop Field to use DC on M1–M4.</div>
    <div class="toolbar" id="dcPresetToolbar">
      <button type="button" id="btnContourOn" onclick="sendContour(true)">Contour ON (M1–M4)</button>
      <button type="button" id="btnContourOff" onclick="sendContour(false)">Contour OFF</button>
      <button type="button" id="btnProgP1" onclick="sendProgram('P1')">Program P1</button>
      <button type="button" id="btnProgP2" onclick="sendProgram('P2')">Program P2</button>
      <button type="button" class="danger" onclick="allOff()">ALL OFF</button>
    </div>
    <div class="cfg-grid" style="margin-bottom:14px">
      <div class="card">
        <h2>DC control mode</h2>
        <div class="seg">
          <button type="button" id="modeOpenBtn" class="active" onclick="setMode('open')">Open-loop</button>
          <button type="button" id="modeClosedBtn" onclick="setMode('closed')">Closed-loop</button>
        </div>
        <div class="kv" style="margin-top:10px">Active: <b id="cfgModeLabel">open</b> · contour <b id="cfgContourLabel">off</b></div>
      </div>
    </div>
    <div class="grid" id="coils"></div>
  </section>

  <section id="tab-logs" class="tab-panel">
    <h2 class="section-title">Command log</h2>
    <p class="section-help">Bridge traffic and raw STM32 CLI. Useful for lab debugging.</p>
    <div class="log-toolbar">
      <input id="rawCmd" type="text" placeholder="e.g. rotate on   or   freq 5   or   status"
        onkeydown="if(event.key==='Enter')sendRawCmd()"/>
      <button type="button" class="primary" onclick="sendRawCmd()">Send</button>
      <button type="button" onclick="clearLog()">Clear</button>
    </div>
    <div class="log" id="log">[ready] join SoftAP CoilControl and wait for linked status…</div>
  </section>
</main>

<script>
const N = 4;
let state = {
  mode: 'open', contour: 0, rotate: 0, axis: 'z', axis_live: 'z',
  freq: 1.0, amp: 0, amp_live: 0, rot_dir: 'cw', rot_dir_live: 'cw', coils: []
};
for (let i = 1; i <= N; i++) {
  state.coils.push({ id: i, en: 0, mag: 0, dir: 1, duty: 0, i: 0, lim: 2 });
}
/* Prevent poll from clobbering in-progress / unconfirmed setpoint edits */
let freqDirtyUntil = 0;
let ampDirtyUntil = 0;
let pendingFreq = null; /* last freq we want on the device (null = none) */
let pendingAmp = null;
let _resyncBusy = false;

/* Teleplot-style series: M1/M3 Phase A, M2/M4 Phase B */
const WAVE = {
  windowSec: 2.0,
  series: [
    { id: 'M1', color: '#45c4b0', visible: true, phaseKey: 'A' },
    { id: 'M2', color: '#f0a202', visible: true, phaseKey: 'B' },
    { id: 'M3', color: '#6c8cff', visible: true, phaseKey: 'A' },
    { id: 'M4', color: '#e07a5f', visible: true, phaseKey: 'B' }
  ],
  t0: performance.now() / 1000,
  raf: 0
};

function showTab(name) {
  document.querySelectorAll('.tab-panel').forEach(p => p.classList.toggle('active', p.id === 'tab-' + name));
  document.querySelectorAll('.tabs button').forEach(b => b.classList.toggle('active', b.dataset.tab === name));
  if (name === 'field') resizeWaveCanvas();
}

function showBanner(msg, isErr) {
  const el = document.getElementById('msgBanner');
  if (!msg) { el.className = 'banner'; el.textContent = ''; return; }
  el.className = 'banner show' + (isErr ? ' err' : '');
  el.textContent = msg;
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
  const opts = body
    ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }
    : {};
  const r = await fetch(path, opts);
  const text = await r.text();
  let j = {};
  try { j = JSON.parse(text); } catch (_) { j = { raw: text }; }
  if (!r.ok) throw new Error((j.raw && String(j.raw).slice(0, 80)) || ('HTTP ' + r.status));
  return j;
}

async function sendCmd(cmd) {
  try {
    const j = await api('/api/cmd', { cmd });
    log('> ' + cmd + (j.raw ? '  ← ' + String(j.raw).trim().slice(0, 120) : ''));
    if (j.status) applyStatus(j.status);
    if (j.ok !== false) {
      showBanner('');
      if (j.linked === true) setLink(true);
      return true;
    }
    showBanner('Command reply noisy or empty — retrying…', true);
    /* One UI-level retry for flaky SoftAP/UART */
    try {
      const j2 = await api('/api/cmd', { cmd });
      log('> ' + cmd + ' (retry)' + (j2.raw ? '  ← ' + String(j2.raw).trim().slice(0, 120) : ''));
      if (j2.status) applyStatus(j2.status);
      if (j2.ok !== false) {
        showBanner('');
        if (j2.linked === true) setLink(true);
        return true;
      }
    } catch (_) {}
    showBanner('Command reply noisy or empty — check link', true);
    syncLabels();
    return false;
  } catch (e) {
    log('ERR ' + e.message);
    setLink(false, 'cmd_fetch');
    showBanner(e.message, true);
    return false;
  }
}

async function sendRawCmd() {
  const el = document.getElementById('rawCmd');
  const cmd = (el.value || '').trim();
  if (!cmd) return;
  el.value = '';
  await sendCmd(cmd);
}

async function allOff() {
  await sendCmd('rotate off');
  await sendCmd('off all');
}

async function setRotate(on) {
  const ok = await sendCmd(on ? 'rotate on' : 'rotate off');
  if (!ok) {
    showBanner(on ? 'START failed — rotate state unchanged' : 'STOP failed — check link', true);
  }
  syncLabels();
}

async function sendContour(on) {
  if (state.rotate) {
    showBanner('Stop rotating field before contour (M1–M4 DC)', true);
    return;
  }
  await sendCmd(on ? 'contour on' : 'contour off');
}

async function sendProgram(which) {
  if (state.rotate) {
    showBanner('Stop rotating field before program ' + which, true);
    return;
  }
  await sendCmd('program ' + which);
}

async function applyFreq() {
  let f = +document.getElementById('freqVal').value;
  if (f < 0.1) f = 0.1;
  if (f > 50) f = 50;
  state.freq = f;
  pendingFreq = f;
  freqDirtyUntil = Date.now() + 15000;
  document.getElementById('freqVal').value = f.toFixed(2);
  document.getElementById('freqLabel').textContent = f.toFixed(2);
  document.getElementById('freqRange').value = Math.round(f * 10);
  const ok = await sendCmd('freq ' + f.toFixed(2));
  if (!ok) {
    showBanner('Freq command weak — holding ' + f.toFixed(1) + ' Hz until confirmed', true);
  }
  syncLabels();
}

function syncFreqFromRange(v) {
  onFreqInput((+v) / 10);
}

function onFreqInput(f) {
  if (!Number.isFinite(f)) return;
  if (f < 0.1) f = 0.1;
  if (f > 50) f = 50;
  state.freq = f;
  /* Don't set pending until applyFreq sends — avoid mid-drag resync storms */
  freqDirtyUntil = Date.now() + 15000;
  document.getElementById('freqVal').value = f.toFixed(1);
  document.getElementById('freqLabel').textContent = f.toFixed(2);
  document.getElementById('freqRange').value = Math.round(f * 10);
}

function onAmpInput(a) {
  if (!Number.isFinite(a)) return;
  if (a < 0) a = 0;
  if (a > 255) a = 255;
  a = Math.round(a);
  state.amp = a;
  ampDirtyUntil = Date.now() + 15000;
  document.getElementById('ampVal').value = a;
  document.getElementById('ampLabel').textContent = a;
  document.getElementById('ampRange').value = a;
}

async function applyAmp() {
  let a = +document.getElementById('ampVal').value;
  if (a < 0) a = 0;
  if (a > 255) a = 255;
  a = Math.round(a);
  state.amp = a;
  pendingAmp = a;
  ampDirtyUntil = Date.now() + 15000;
  document.getElementById('ampVal').value = a;
  document.getElementById('ampLabel').textContent = a;
  document.getElementById('ampRange').value = a;
  const ok = await sendCmd('amp ' + a);
  if (!ok) {
    showBanner('Amp command weak — holding ' + a + ' until confirmed', true);
  }
  syncLabels();
}

async function setRotDir(d) {
  const ok = await sendCmd('rot_dir ' + d);
  if (ok) state.rot_dir = d;
  syncLabels();
}

async function setAxis(a) {
  const ok = await sendCmd('axis ' + a);
  if (ok) state.axis = a;
  syncLabels();
}

async function setMode(m) {
  await sendCmd('mode ' + m);
}

async function applyLimit() {
  let v = +document.getElementById('limVal').value;
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  await sendCmd('limit all ' + v);
}

async function applyCoil(i) {
  const c = state.coils[i];
  const dir = c.dir >= 0 ? '+' : '-';
  if (c.en) return await sendCmd(`drive M${c.id} ${c.mag} ${dir}`);
  return await sendCmd(`off M${c.id}`);
}

function avgI(ids) {
  let s = 0, n = 0;
  ids.forEach(id => {
    const c = state.coils[id - 1];
    if (c) { s += (+c.i || 0); n++; }
  });
  return n ? (s / n) : 0;
}

function displayAmp() {
  const busy = pendingAmp != null || Date.now() < ampDirtyUntil;
  if (busy) return state.amp;
  if (state.amp_live != null && Number.isFinite(+state.amp_live)) return +state.amp_live;
  return state.amp;
}

function syncLabels() {
  document.getElementById('cfgModeLabel').textContent = state.mode;
  document.getElementById('cfgContourLabel').textContent = state.contour ? 'ON' : 'off';
  document.getElementById('cfgRotLabel').textContent = state.rotate ? 'ON' : 'off';
  const axis = state.axis || 'z';
  document.getElementById('cfgAxisLabel').textContent = axis.toUpperCase();
  document.getElementById('modeOpenBtn').classList.toggle('active', state.mode === 'open');
  document.getElementById('modeClosedBtn').classList.toggle('active', state.mode === 'closed');
  document.getElementById('dirCwBtn').classList.toggle('active', state.rot_dir === 'cw');
  document.getElementById('dirCcwBtn').classList.toggle('active', state.rot_dir === 'ccw');
  const az = document.getElementById('axisZBtn');
  const ax = document.getElementById('axisXBtn');
  if (az) az.classList.toggle('active', axis === 'z');
  if (ax) ax.classList.toggle('active', axis === 'x');

  const shownAmp = displayAmp();
  document.getElementById('stRot').textContent = state.rotate ? 'ON' : 'off';
  document.getElementById('stAxis').textContent = axis;
  document.getElementById('stFreq').textContent = (+state.freq).toFixed(2);
  document.getElementById('stAmp').textContent = shownAmp;
  document.getElementById('stDir').textContent = state.rot_dir;
  document.getElementById('stMode').textContent = state.mode;

  const ia = avgI([1, 3]);
  const ib = avgI([2, 4]);
  document.getElementById('stIA').textContent = ia.toFixed(3) + ' A';
  document.getElementById('stIB').textContent = ib.toFixed(3) + ' A';
  document.getElementById('phaseAI').textContent = ia.toFixed(3);
  document.getElementById('phaseBI').textContent = ib.toFixed(3);

  const paRole = document.getElementById('phaseARole');
  const pbRole = document.getElementById('phaseBRole');
  if (paRole && pbRole) {
    if (axis === 'x') {
      paRole.textContent = 'X: idle (axis of rotation)';
      pbRole.textContent = 'X: in-phase → net Bz';
    } else {
      paRole.textContent = 'Z: Phase A @ 0°';
      pbRole.textContent = 'Z: Phase B @ ±90°';
    }
  }

  const hint = document.getElementById('plotRunHint');
  if (hint) {
    if (!state.rotate) hint.textContent = 'rotate off → flat';
    else if (!(+shownAmp > 0) && !(+state.amp > 0)) hint.textContent = 'rotate ON but amp=0 → set Amplitude to see waves';
    else {
      const derated = state.amp_live != null && +state.amp_live < +state.amp;
      const flipping = state.rot_dir_live && state.rot_dir && state.rot_dir_live !== state.rot_dir;
      const axisFlip = state.axis_live && state.axis && state.axis_live !== state.axis;
      const ramping = derated || flipping || axisFlip || (+shownAmp !== +state.amp);
      hint.textContent = `rotate ON · ${axis.toUpperCase()} · ${(+state.freq).toFixed(2)} Hz · amp ${shownAmp}` +
        (derated || ramping ? ` (cmd ${state.amp})` : '') +
        ` · ${flipping ? state.rot_dir_live + '→' + state.rot_dir : state.rot_dir}` +
        (ramping ? ' · ramping' : '');
    }
  }

  const freqEl = document.getElementById('freqVal');
  const ampEl = document.getElementById('ampVal');
  const freqBusy = pendingFreq != null || Date.now() < freqDirtyUntil ||
    document.activeElement === freqEl ||
    document.activeElement === document.getElementById('freqRange');
  const ampBusy = pendingAmp != null || Date.now() < ampDirtyUntil ||
    document.activeElement === ampEl ||
    document.activeElement === document.getElementById('ampRange');
  if (!freqBusy) {
    freqEl.value = (+state.freq).toFixed(2);
    document.getElementById('freqLabel').textContent = (+state.freq).toFixed(2);
    document.getElementById('freqRange').value = Math.round((+state.freq) * 10);
  } else if (pendingFreq != null) {
    freqEl.value = (+pendingFreq).toFixed(2);
    document.getElementById('freqLabel').textContent = (+pendingFreq).toFixed(2);
    document.getElementById('freqRange').value = Math.round((+pendingFreq) * 10);
  }
  if (!ampBusy) {
    ampEl.value = state.amp;
    document.getElementById('ampLabel').textContent = state.amp;
    document.getElementById('ampRange').value = state.amp;
  } else if (pendingAmp != null) {
    ampEl.value = pendingAmp;
    document.getElementById('ampLabel').textContent = pendingAmp;
    document.getElementById('ampRange').value = pendingAmp;
  }

  const lock = document.getElementById('dcLockBanner');
  if (state.rotate) {
    lock.style.display = 'block';
    lock.className = 'banner show';
  } else {
    lock.style.display = 'none';
  }

  const rotLock = !!state.rotate;
  ['btnContourOn', 'btnContourOff', 'btnProgP1', 'btnProgP2'].forEach(id => {
    const b = document.getElementById(id);
    if (b) b.disabled = rotLock;
  });
}

function applyStatus(s) {
  if (!s || !s.coils) return;
  state.mode = s.mode || state.mode;
  state.contour = s.contour || 0;
  state.rotate = s.rotate || 0;

  const freqBusy = pendingFreq != null || Date.now() < freqDirtyUntil ||
    document.activeElement === document.getElementById('freqVal') ||
    document.activeElement === document.getElementById('freqRange');
  const ampBusy = pendingAmp != null || Date.now() < ampDirtyUntil ||
    document.activeElement === document.getElementById('ampVal') ||
    document.activeElement === document.getElementById('ampRange');

  if (s.freq != null && Number.isFinite(+s.freq)) {
    const f = +s.freq;
    if (pendingFreq != null) {
      if (Math.abs(f - pendingFreq) < 0.15) {
        state.freq = f;
        pendingFreq = null;
        freqDirtyUntil = 0;
      } else {
        state.freq = pendingFreq;
        scheduleResync();
      }
    } else if (!freqBusy) {
      state.freq = f;
    }
  }

  if (s.amp != null && Number.isFinite(+s.amp)) {
    const a = +s.amp;
    if (pendingAmp != null) {
      if (Math.abs(a - pendingAmp) < 0.5) {
        state.amp = a;
        pendingAmp = null;
        ampDirtyUntil = 0;
      } else {
        state.amp = pendingAmp;
        scheduleResync();
      }
    } else if (!ampBusy) {
      state.amp = a;
    }
  }

  if (s.amp_live != null && Number.isFinite(+s.amp_live)) {
    state.amp_live = +s.amp_live;
  } else if (!ampBusy) {
    state.amp_live = state.amp;
  }

  if (s.rot_dir) state.rot_dir = s.rot_dir;
  if (s.rot_dir_live) state.rot_dir_live = s.rot_dir_live;
  else if (s.rot_dir) state.rot_dir_live = s.rot_dir;
  if (s.axis) state.axis = s.axis;
  if (s.axis_live) state.axis_live = s.axis_live;
  else if (s.axis) state.axis_live = s.axis;
  s.coils.forEach((c, idx) => {
    if (!state.coils[idx]) return;
    Object.assign(state.coils[idx], c);
  });
  render(false);
  syncLabels();
  setLink(true);
}

function scheduleResync() {
  if (_resyncBusy) return;
  _resyncBusy = true;
  setTimeout(async () => {
    try {
      if (pendingFreq != null) {
        await sendCmd('freq ' + (+pendingFreq).toFixed(2));
      }
      if (pendingAmp != null) {
        await sendCmd('amp ' + Math.round(+pendingAmp));
      }
    } finally {
      _resyncBusy = false;
    }
  }, 400);
}

function setLink(ok, reason) {
  document.getElementById('linkDot').className = 'dot' + (ok ? ' ok' : '');
  if (ok) {
    document.getElementById('linkText').textContent =
      `linked · ${state.rotate ? 'ROT' : 'DC'} · ${state.mode}`;
  } else if (reason === 'uart_only') {
    document.getElementById('linkText').textContent = 'UART up · status stale';
  } else {
    document.getElementById('linkText').textContent = 'STM32 link lost';
  }
}

function render(rebuild) {
  const root = document.getElementById('coils');
  const rotLock = !!state.rotate;
  if (rebuild || !root.children.length) {
    root.innerHTML = '';
    state.coils.forEach((c, idx) => {
      const locked = rotLock && idx < 4;
      const d = document.createElement('div');
      d.className = 'card' + (c.en ? ' live' : '') + (locked ? ' disabled' : '');
      d.id = 'card' + idx;
      d.innerHTML = `
        <div class="card-head">
          <h2>Coil M${c.id}${idx % 2 === 0 ? ' · X' : ' · Y'}</h2>
          <label class="switch" title="Enable">
            <input type="checkbox" ${c.en ? 'checked' : ''} ${locked ? 'disabled' : ''}
              onchange="toggleEn(${idx}, this.checked)">
            <span class="slider"></span>
          </label>
        </div>
        <label class="row"><span>Magnitude</span><b id="magv${idx}">${c.mag}</b></label>
        <input type="range" min="0" max="255" value="${c.mag}" ${locked ? 'disabled' : ''}
          oninput="document.getElementById('magv${idx}').textContent=this.value"
          onchange="setMag(${idx}, +this.value)">
        <label class="row"><span>Direction (B-field)</span></label>
        <div class="dir">
          <button type="button" class="fwd ${c.dir>=0?'active':''}" ${locked ? 'disabled' : ''}
            onclick="setDir(${idx},1)">Forward +</button>
          <button type="button" class="rev ${c.dir<0?'active':''}" ${locked ? 'disabled' : ''}
            onclick="setDir(${idx},-1)">Reverse −</button>
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
      const locked = rotLock && idx < 4;
      card.className = 'card' + (c.en ? ' live' : '') + (locked ? ' disabled' : '');
      const en = card.querySelector('input[type=checkbox]');
      if (en) { en.checked = !!c.en; en.disabled = locked; }
      const range = card.querySelector('input[type=range]');
      if (range) {
        range.disabled = locked;
        if (document.activeElement !== range) range.value = c.mag;
      }
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
      buttons[0].disabled = locked;
      buttons[1].disabled = locked;
    });
  }
}

async function toggleEn(i, on) {
  const prev = state.coils[i].en;
  state.coils[i].en = on ? 1 : 0;
  const ok = await applyCoil(i);
  if (ok === false) state.coils[i].en = prev;
  render(false);
}
async function setMag(i, v) {
  const prev = state.coils[i].mag;
  state.coils[i].mag = v;
  let ok = true;
  if (state.coils[i].en) ok = await applyCoil(i);
  else ok = await sendCmd(`set M${state.coils[i].id} ${v}`);
  if (ok === false) state.coils[i].mag = prev;
  render(false);
}
async function setDir(i, d) {
  const prev = state.coils[i].dir;
  state.coils[i].dir = d;
  let ok = true;
  if (state.coils[i].en) ok = await applyCoil(i);
  else ok = await sendCmd(`dir M${state.coils[i].id} ${d>=0?'+':'-'}`);
  if (ok === false) state.coils[i].dir = prev;
  render(false);
}

let _pollBusy = false;
async function poll() {
  if (_pollBusy) return;
  _pollBusy = true;
  try {
    const j = await api('/api/status');
    if (j.status) {
      applyStatus(j.status);
      setLink(true);
    } else if (j.uart_ok) {
      setLink(false, 'uart_only');
    } else {
      setLink(false, 'status_unlinked');
    }
    syncLabels();
  } catch (e) {
    setLink(false, 'poll_fetch');
  } finally {
    _pollBusy = false;
  }
}

function buildPlotLegend() {
  const root = document.getElementById('plotLegend');
  if (!root) return;
  root.innerHTML = '';
  WAVE.series.forEach((s, i) => {
    const lab = document.createElement('label');
    lab.className = s.visible ? 'on' : 'off';
    lab.innerHTML =
      `<input type="checkbox" ${s.visible ? 'checked' : ''} onchange="toggleWave(${i}, this.checked)">` +
      `<span class="swatch" style="background:${s.color}"></span>${s.id}`;
    root.appendChild(lab);
  });
}

function toggleWave(i, on) {
  WAVE.series[i].visible = !!on;
  buildPlotLegend();
}

function resizeWaveCanvas() {
  const canvas = document.getElementById('waveCanvas');
  const wrap = canvas && canvas.parentElement;
  if (!canvas || !wrap) return;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const w = wrap.clientWidth || 600;
  const h = wrap.clientHeight || 220;
  canvas.width = Math.floor(w * dpr);
  canvas.height = Math.floor(h * dpr);
  canvas.style.width = w + 'px';
  canvas.style.height = h + 'px';
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function coilDutyAt(tSec, phaseKey) {
  /* Preview live waveform when rotate is on (amp=0 → flat). */
  if (!state.rotate) return 0;
  const f = Math.max(0.1, +state.freq || 0);
  const amp = Math.max(0, Math.min(255, +displayAmp() || 0));
  const theta = 2 * Math.PI * f * tSec;
  const liveDir = state.rot_dir_live || state.rot_dir;
  const axis = state.axis_live || state.axis || 'z';
  if (axis === 'x') {
    /* M1/M3 (A) idle; M2/M4 (B) identical sine */
    if (phaseKey === 'A') return 0;
    const s = amp * Math.sin(theta);
    return liveDir === 'ccw' ? -s : s;
  }
  if (phaseKey === 'A') return amp * Math.sin(theta);
  const c = Math.cos(theta);
  return amp * (liveDir === 'ccw' ? -c : c);
}

function drawWavePlot() {
  const canvas = document.getElementById('waveCanvas');
  if (!canvas) return;
  const wrap = canvas.parentElement;
  const W = wrap.clientWidth || 600;
  const H = wrap.clientHeight || 220;
  if (canvas.width === 0) resizeWaveCanvas();
  const ctx = canvas.getContext('2d');
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

  /* Teleplot-like dark canvas */
  ctx.fillStyle = '#0b0f13';
  ctx.fillRect(0, 0, W, H);

  const padL = 44, padR = 10, padT = 10, padB = 22;
  const plotW = Math.max(10, W - padL - padR);
  const plotH = Math.max(10, H - padT - padB);
  const yMin = -255, yMax = 255;
  const tNow = performance.now() / 1000 - WAVE.t0;
  const tWin = WAVE.windowSec;
  const tStart = tNow - tWin;

  function xOf(t) { return padL + ((t - tStart) / tWin) * plotW; }
  function yOf(v) { return padT + (1 - (v - yMin) / (yMax - yMin)) * plotH; }

  /* Grid */
  ctx.strokeStyle = '#1c2530';
  ctx.lineWidth = 1;
  ctx.beginPath();
  for (let g = 0; g <= 4; g++) {
    const y = padT + (plotH * g) / 4;
    ctx.moveTo(padL, y); ctx.lineTo(padL + plotW, y);
  }
  for (let g = 0; g <= 4; g++) {
    const x = padL + (plotW * g) / 4;
    ctx.moveTo(x, padT); ctx.lineTo(x, padT + plotH);
  }
  ctx.stroke();

  /* Zero line */
  ctx.strokeStyle = '#3a4a58';
  ctx.beginPath();
  ctx.moveTo(padL, yOf(0));
  ctx.lineTo(padL + plotW, yOf(0));
  ctx.stroke();

  /* Axis labels */
  ctx.fillStyle = '#6b7c8a';
  ctx.font = '11px ui-monospace, Menlo, monospace';
  ctx.textAlign = 'right';
  ctx.fillText('+255', padL - 6, padT + 10);
  ctx.fillText('0', padL - 6, yOf(0) + 3);
  ctx.fillText('-255', padL - 6, padT + plotH);
  ctx.textAlign = 'center';
  ctx.fillText((-tWin).toFixed(1) + 's', padL, H - 6);
  ctx.fillText('0', padL + plotW, H - 6);

  /* Traces — denser samples for smooth Teleplot look */
  const steps = Math.min(900, Math.max(200, Math.floor(plotW)));
  WAVE.series.forEach(s => {
    if (!s.visible) return;
    ctx.strokeStyle = s.color;
    ctx.lineWidth = 1.6;
    ctx.beginPath();
    for (let i = 0; i <= steps; i++) {
      const t = tStart + (tWin * i) / steps;
      const v = coilDutyAt(t, s.phaseKey);
      const x = xOf(t), y = yOf(v);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();
  });

  /* Live cursor */
  ctx.strokeStyle = '#5a6a78';
  ctx.setLineDash([3, 3]);
  ctx.beginPath();
  ctx.moveTo(padL + plotW, padT);
  ctx.lineTo(padL + plotW, padT + plotH);
  ctx.stroke();
  ctx.setLineDash([]);
}

function waveLoop() {
  drawWavePlot();
  WAVE.raf = requestAnimationFrame(waveLoop);
}

showTab('field');
render(true);
syncLabels();
buildPlotLegend();
resizeWaveCanvas();
window.addEventListener('resize', resizeWaveCanvas);
document.getElementById('plotWin').textContent = WAVE.windowSec.toFixed(1);
waveLoop();
poll();
setInterval(poll, 2000);
</script>
</body>
</html>
)HTML";
