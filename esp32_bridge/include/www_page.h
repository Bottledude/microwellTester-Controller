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
  .seg.quad { grid-template-columns: 1fr 1fr; }
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
  <span>field <b id="stRot">off</b></span>
  <span>prog <b id="stProg">rotate</b></span>
  <span>axis <b id="stAxis">y</b></span>
  <span>freq <b id="stFreq">1.00</b> Hz</span>
  <span>amp <b id="stAmp">0</b></span>
  <span>H/V <b id="stHV">100/100</b></span>
  <span>γ <b id="stGamma">45</b>°</span>
  <span>dir <b id="stDir">cw</b></span>
  <span>mode <b id="stMode">open</b></span>
  <span>I<sub>1</sub> <b id="stIA">—</b></span>
  <span>I<sub>2</sub> <b id="stIB">—</b></span>
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
        <h2>Coil drive waveforms</h2>
        <div class="plot-legend" id="plotLegend"></div>
      </div>
      <div class="seg" style="margin-bottom:8px;max-width:420px">
        <button type="button" id="plotBipolarBtn" class="active" onclick="setPlotMode('bipolar')">Bipolar (sine cmd)</button>
        <button type="button" id="plotUnipolarBtn" onclick="setPlotMode('unipolar')">Unipolar (RPWM/LPWM)</button>
      </div>
      <div class="plot-wrap"><canvas id="waveCanvas"></canvas></div>
      <div class="plot-meta">
        <span>window <b id="plotWin">2.0</b> s</span>
        <span id="plotYLabel">y: signed duty <b>±255</b></span>
        <span>source <b id="plotSource">math preview</b></span>
        <span id="plotRunHint">field off → flat</span>
      </div>
      <div class="kv" style="margin-top:8px">
        Live firmware snapshot (from <code>json</code>): <b id="signedSnap">—</b><br/>
        MLX reconstructed centre (mT): <b id="mlxSnap">—</b><br/>
        Live scope: USB Serial Monitor + Teleplot — type <code>teleplot on</code> on USART1.
        PWM-proxy series <code>Bx/By/Babs</code>; measured <code>Bx_m/By_m/Babs_m</code>.
      </div>
    </div>
    <h2 class="section-title">Rotate / revolve</h2>
    <p class="section-help">Y is vertical (out of plane); X and Z are horizontal. Coils at 90°, each tilted 30° from horizontal. <b>Rotate Y</b>: field in XZ — M1=M3 ∝ cos ωt, M2=M4 ∝ sin ωt. <b>Revolve X/Z/tilt</b> share a four-coil 30° inverse: the secondary pair supplies B<sub>y</sub> so |B<sub>v</sub>| matches |B<sub>h</sub>| at ±Y without cutting horizontal amp. X = γ=0 (YZ), Z = γ=90 (XY). Hall chips behind each coil reconstruct centre B for display only. ALL OFF still cuts immediately.</p>
    <div class="cfg-grid">
      <div class="card">
        <h2>Run control</h2>
        <div class="toolbar" style="margin:0">
          <button type="button" class="primary large" id="btnRotOn" onclick="setFieldRun(true)">START</button>
          <button type="button" class="danger large" id="btnRotOff" onclick="setFieldRun(false)">STOP</button>
        </div>
        <div class="kv" style="margin-top:12px">State: <b id="cfgRotLabel">off</b> · <b id="cfgAxisLabel">rotate Y</b></div>
        <div class="phase-map">
          <div class="phase-box">
            <div class="t" id="phaseATitle">M1 ∥ M3 · cos ωt</div>
            <div class="v" id="phaseARole">Rotate Y · XZ</div>
            <div class="kv" style="margin-top:6px">I ≈ <b id="phaseAI">0.000</b> A</div>
          </div>
          <div class="phase-box">
            <div class="t" id="phaseBTitle">M2 ∥ M4 · sin ωt</div>
            <div class="v" id="phaseBRole">Rotate Y · XZ</div>
            <div class="kv" style="margin-top:6px">I ≈ <b id="phaseBI">0.000</b> A</div>
          </div>
        </div>
      </div>

      <div class="card">
        <h2>Field program</h2>
        <p class="section-help">All programs use four coils. Revolve X is γ=0 (YZ). Revolve Z is γ=90 (XY). Revolve tilt uses the γ slider.</p>
        <div class="seg quad">
          <button type="button" id="progRotYBtn" class="active" onclick="setFieldProgram('rotate','y')">Rotate Y</button>
          <button type="button" id="progRevXBtn" onclick="setFieldProgram('revolve','x')">Revolve X</button>
          <button type="button" id="progRevZBtn" onclick="setFieldProgram('revolve','z')">Revolve Z</button>
          <button type="button" id="progRevTiltBtn" onclick="setFieldProgram('revolve','tilt')">Revolve tilt</button>
        </div>
      </div>

      <div class="card">
        <h2>Frequency</h2>
        <p class="section-help">Excitation 0.01–50 Hz. PCA9685 carrier is ~1 kHz.</p>
        <div class="field">
          <label for="freqVal">Frequency (Hz) <b id="freqLabel">1.00</b></label>
          <input id="freqVal" type="number" min="0.01" max="50" step="0.01" value="1.0"
            oninput="onFreqInput(+this.value)"
            onchange="applyFreq()"/>
        </div>
        <input type="range" id="freqRange" min="1" max="5000" value="100"
          oninput="syncFreqFromRange(this.value)"
          onchange="applyFreq()"/>
        <div class="kv" style="margin-top:8px">Slider = 0.01–50.00 Hz (×0.01)</div>
      </div>

      <div class="card">
        <h2>Amplitude</h2>
        <p class="section-help">Shared peak duty 0–255 (Bmax/μ scale). Rotate Y and all revolve programs drive M1–M4.</p>
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
        <h2>Revolve A scale (H / V)</h2>
        <p class="section-help">A<sub>h</sub>=amp·H/100, A<sub>v</sub>=amp·V/100. 100/100 is a PWM-proxy circle (geometry no longer requires cutting H). Unequal radii are an ellipse. Rotate Y ignores this. Shared-scale if a coil would exceed 255.</p>
        <div class="field">
          <label for="revHVal">Horizontal % <b id="revHLabel">100</b></label>
          <input id="revHVal" type="number" min="1" max="200" step="1" value="100"
            oninput="onRevHInput(+this.value)"
            onchange="applyRevHV()"/>
        </div>
        <input type="range" id="revHRange" min="1" max="200" value="100"
          oninput="onRevHInput(+this.value)"
          onchange="applyRevHV()"/>
        <div class="field">
          <label for="revVVal">Vertical % <b id="revVLabel">100</b></label>
          <input id="revVVal" type="number" min="1" max="200" step="1" value="100"
            oninput="onRevVInput(+this.value)"
            onchange="applyRevHV()"/>
        </div>
        <input type="range" id="revVRange" min="1" max="200" value="100"
          oninput="onRevVInput(+this.value)"
          onchange="applyRevHV()"/>
      </div>

      <div class="card">
        <h2>Tilt axis γ</h2>
        <p class="section-help">Fixed rotation axis in the XZ plane, measured from +X toward +Z (0–90°). 0° = revolve-X plane (YZ); 90° = revolve-Z plane (XY) with Q0 at −X. Changing γ while running ramps through 0. Does not rotate the axis over time.</p>
        <div class="field">
          <label for="gammaVal">γ (deg) <b id="gammaLabel">45</b></label>
          <input id="gammaVal" type="number" min="0" max="90" step="1" value="45"
            oninput="onGammaInput(+this.value)"
            onchange="applyGamma()"/>
        </div>
        <input type="range" id="gammaRange" min="0" max="90" value="45"
          oninput="onGammaInput(+this.value)"
          onchange="applyGamma()"/>
      </div>

      <div class="card">
        <h2>MLX90393 behind coils</h2>
        <p class="section-help">Four chips on I2C2 (PB10/PB11, 3.3 V), one behind each coil. Centre B is reconstructed with the 30° mixing map. Display only — never drives PWM. Offset zeros Earth/bias with coils off.</p>
        <div class="kv">Present: <b id="mlxPresent">—</b><br/>
          Centre (mT): <b id="mlxB">—</b><br/>
          Coils: <b id="mlxCoils">—</b>
        </div>
        <div class="toolbar" style="margin-top:12px;margin-bottom:0;flex-wrap:wrap">
          <button type="button" onclick="sendCmd('mlx scan')">Scan</button>
          <button type="button" onclick="sendCmd('mlx offset')">Offset</button>
        </div>
      </div>

      <div class="card">
        <h2>Revolve corner</h2>
        <p class="section-help">Park at a cardinal (sine-travel along the |B| circle). <b>Run</b> resumes revolving. Q0=+B<sub>h</sub> (+h in tilt), Q1=+B<sub>v</sub> (+Y), Q2=−B<sub>h</sub>, Q3=−B<sub>v</sub>.</p>
        <div class="seg" style="grid-template-columns:repeat(5,1fr);max-width:520px">
          <button type="button" id="cornerQ0Btn" onclick="setCorner(0)">Q0</button>
          <button type="button" id="cornerQ1Btn" onclick="setCorner(1)">Q1</button>
          <button type="button" id="cornerQ2Btn" onclick="setCorner(2)">Q2</button>
          <button type="button" id="cornerQ3Btn" onclick="setCorner(3)">Q3</button>
          <button type="button" id="cornerRunBtn" class="active" onclick="setCorner(-1)">Run</button>
        </div>
        <div class="kv" style="margin-top:8px">Now: <b id="cornerLabel">run</b></div>
      </div>

      <div class="card">
        <h2>Rotation direction</h2>
        <p class="section-help">Rotate Y: CW = M2∥M4 = +sin, CCW = −sin. Revolve X/Z: CCW runs the circle backward (−ω). Tilt: CW = +θ (ω = −n); CCW = −θ (ω = +n).</p>
        <div class="seg">
          <button type="button" id="dirCwBtn" class="active" onclick="setRotDir('cw')">CW</button>
          <button type="button" id="dirCcwBtn" onclick="setRotDir('ccw')">CCW</button>
        </div>
      </div>

      <div class="card">
        <h2>Soft current limit</h2>
        <p class="section-help">0–255 ≈ 0–2 A. Applied to all coils. While the AC field is on, over-limit reduces live amplitude.</p>
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
          ESP32 TX17 → PD9 · RX18 ← PD8<br/>
          PCA I2C1 PB8 SCL / PB9 SDA (0x40) · OE to GND<br/>
          MLX I2C2 PB10 SCL / PB11 SDA · VIN 3.3 V · CS high<br/>
          PWM ch0/1 M1 · 2/3 M2 · 4/5 M3 · 6/7 M4<br/>
          EN M1 PD10 · M2 PE4 · M3 PC4 · M4 PC5 · IS PA0–PA3
        </div>
        <div class="toolbar" style="margin-top:12px;margin-bottom:0">
          <button type="button" onclick="poll()">Refresh status</button>
        </div>
      </div>
    </div>
  </section>

  <section id="tab-dc" class="tab-panel">
    <h2 class="section-title">DC coil drivers</h2>
    <p class="section-help">Constant magnitude / direction (not sine). All four coils lock while rotate/revolve is active.</p>
    <div class="banner show" id="dcLockBanner" style="display:none;margin:0 0 12px">AC field active — stop Field to use DC on M1–M4.</div>
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
      <input id="rawCmd" type="text" placeholder="e.g. rotate on   or   revolve z 2 160   or   status"
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
  mode: 'open', contour: 0, rotate: 0,
  program: 'rotate', program_live: 'rotate',
  axis: 'y', axis_live: 'y',
  freq: 1.0, amp: 0, amp_live: 0,
  rev_h: 100, rev_v: 100,
  gamma: 45, gamma_live: 45, tilt_scale: 1,
  rot_dir: 'cw', rot_dir_live: 'cw',
  corner: -1, park: 0, travel: 0,
  signed: [0, 0, 0, 0], pr: [0, 0, 0, 0], pl: [0, 0, 0, 0],
  mlx: null,
  coils: []
};
for (let i = 1; i <= N; i++) {
  state.coils.push({ id: i, en: 0, mag: 0, dir: 1, duty: 0, i: 0, lim: 2 });
}
/* Prevent poll from clobbering in-progress / unconfirmed setpoint edits */
let freqDirtyUntil = 0;
let ampDirtyUntil = 0;
let revHVDirtyUntil = 0;
let gammaDirtyUntil = 0;
let pendingFreq = null; /* last freq we want on the device (null = none) */
let pendingAmp = null;
let pendingRevHV = null; /* {h,v} or null */
let pendingGamma = null;
let _resyncBusy = false;
let _dbgLastStatus = 0;
function dbgLog(hypothesisId, location, message, data) {
  // #region agent log
  fetch('http://127.0.0.1:7674/ingest/d727dac1-2460-4f44-b40b-e67070044e76',{method:'POST',headers:{'Content-Type':'application/json','X-Debug-Session-Id':'6e46c5'},body:JSON.stringify({sessionId:'6e46c5',runId:'run1',hypothesisId:hypothesisId,location:location,message:message,data:data||{},timestamp:Date.now()})}).catch(()=>{});
  // #endregion
}

/* Teleplot-style series: bipolar = signed cmd; unipolar expands RPWM/LPWM. */
const WAVE = {
  windowSec: 2.0,
  mode: 'bipolar', /* bipolar | unipolar */
  seriesBipolar: [
    { id: 'M1', color: '#45c4b0', visible: true, coil: 1 },
    { id: 'M2', color: '#f0a202', visible: true, coil: 2 },
    { id: 'M3', color: '#6c8cff', visible: true, coil: 3 },
    { id: 'M4', color: '#e07a5f', visible: true, coil: 4 }
  ],
  seriesUnipolar: [
    { id: 'M1_RPWM', color: '#45c4b0', visible: true, coil: 1, leg: 'rpwm' },
    { id: 'M1_LPWM', color: '#2a7a6e', visible: true, coil: 1, leg: 'lpwm' },
    { id: 'M2_RPWM', color: '#f0a202', visible: true, coil: 2, leg: 'rpwm' },
    { id: 'M2_LPWM', color: '#a66e00', visible: true, coil: 2, leg: 'lpwm' },
    { id: 'M3_RPWM', color: '#6c8cff', visible: true, coil: 3, leg: 'rpwm' },
    { id: 'M3_LPWM', color: '#3d528f', visible: true, coil: 3, leg: 'lpwm' },
    { id: 'M4_RPWM', color: '#e07a5f', visible: true, coil: 4, leg: 'rpwm' },
    { id: 'M4_LPWM', color: '#8f4a38', visible: true, coil: 4, leg: 'lpwm' }
  ],
  t0: performance.now() / 1000,
  raf: 0
};
function waveSeries() {
  return WAVE.mode === 'unipolar' ? WAVE.seriesUnipolar : WAVE.seriesBipolar;
}

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
    // #region agent log
    dbgLog('H4', 'www_page.h:sendCmd', 'cmd reply', {cmd:cmd, ok:j.ok, linked:j.linked, asciiPct:j.asciiPct, raw:(j.raw||'').toString().slice(0,160)});
    // #endregion
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
  /* off all already stops the AC field; no ramped rotate off round trip first. */
  await sendCmd('off all');
}

function isRevolveAxis(ax) {
  return ax === 'x' || ax === 'z' || ax === 'tilt';
}

function liveProgram() {
  if (state.program_live) return state.program_live;
  if (state.program) return state.program;
  const ax = state.axis_live || state.axis || 'y';
  return isRevolveAxis(ax) ? 'revolve' : 'rotate';
}

function liveAxis() {
  return state.axis_live || state.axis || 'y';
}

function fieldCaption(prog, axis) {
  if (prog === 'revolve') {
    if (axis === 'tilt') return 'revolve tilt';
    return 'revolve ' + String(axis || 'z').toUpperCase();
  }
  return 'rotate Y';
}

async function setFieldRun(on) {
  // #region agent log
  dbgLog('H1', 'www_page.h:setFieldRun', 'field cmd', {on:!!on, amp:state.amp, amp_live:state.amp_live, program:state.program, axis:state.axis});
  // #endregion
  let cmd = 'rotate off';
  if (on) {
    const prog = state.program || 'rotate';
    const axis = state.axis || 'y';
    if (prog === 'revolve') {
      if (axis === 'x') cmd = 'revolve x';
      else if (axis === 'tilt') cmd = 'revolve tilt';
      else cmd = 'revolve z';
    } else {
      cmd = 'rotate on';
    }
  }
  const ok = await sendCmd(cmd);
  if (!ok) {
    showBanner(on ? 'START failed — field state unchanged' : 'STOP failed — check link', true);
  }
  syncLabels();
}

async function setFieldProgram(kind, axis) {
  state.program = kind;
  state.axis = axis;
  let ok;
  if (state.rotate) {
    ok = (kind === 'revolve')
      ? await sendCmd('revolve ' + axis)
      : await sendCmd('rotate on');
  } else {
    ok = await sendCmd('axis ' + axis);
  }
  if (ok) {
    state.program = kind;
    state.axis = axis;
  }
  syncLabels();
}

async function sendContour(on) {
  if (state.rotate) {
    showBanner('Stop the AC field before contour (M1–M4 DC)', true);
    return;
  }
  await sendCmd(on ? 'contour on' : 'contour off');
}

async function sendProgram(which) {
  if (state.rotate) {
    showBanner('Stop the AC field before program ' + which, true);
    return;
  }
  await sendCmd('program ' + which);
}

async function applyFreq() {
  let f = +document.getElementById('freqVal').value;
  if (f < 0.01) f = 0.01;
  if (f > 50) f = 50;
  state.freq = f;
  pendingFreq = f;
  freqDirtyUntil = Date.now() + 15000;
  document.getElementById('freqVal').value = f.toFixed(2);
  document.getElementById('freqLabel').textContent = f.toFixed(2);
  document.getElementById('freqRange').value = Math.round(f * 100);
  const ok = await sendCmd('freq ' + f.toFixed(2));
  if (!ok) {
    showBanner('Freq command weak — holding ' + f.toFixed(1) + ' Hz until confirmed', true);
  }
  syncLabels();
}

function syncFreqFromRange(v) {
  onFreqInput((+v) / 100);
}

function onFreqInput(f) {
  if (!Number.isFinite(f)) return;
  if (f < 0.01) f = 0.01;
  if (f > 50) f = 50;
  state.freq = f;
  /* Don't set pending until applyFreq sends — avoid mid-drag resync storms */
  freqDirtyUntil = Date.now() + 15000;
  document.getElementById('freqVal').value = f.toFixed(2);
  document.getElementById('freqLabel').textContent = f.toFixed(2);
  document.getElementById('freqRange').value = Math.round(f * 100);
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

function cornerCaption(q) {
  if (q == null || q < 0) return 'run';
  const lab = ['+Bh', '+Bv', '−Bh', '−Bv'][q] || '';
  return 'Q' + q + ' ' + lab + (state.travel ? ' …' : '');
}

async function setCorner(q) {
  const cmd = (q == null || q < 0) ? 'corner run' : ('corner Q' + q);
  const ok = await sendCmd(cmd);
  if (ok) {
    state.corner = (q == null || q < 0) ? -1 : q;
    state.park = (q >= 0) ? 1 : 0;
    state.travel = 0;
  }
  syncLabels();
}

function clampRevPct(v) {
  if (!Number.isFinite(v)) return 100;
  v = Math.round(v);
  if (v < 1) v = 1;
  if (v > 200) v = 200;
  return v;
}

function onRevHInput(v) {
  v = clampRevPct(v);
  state.rev_h = v;
  revHVDirtyUntil = Date.now() + 15000;
  document.getElementById('revHVal').value = v;
  document.getElementById('revHLabel').textContent = v;
  document.getElementById('revHRange').value = v;
}

function onRevVInput(v) {
  v = clampRevPct(v);
  state.rev_v = v;
  revHVDirtyUntil = Date.now() + 15000;
  document.getElementById('revVVal').value = v;
  document.getElementById('revVLabel').textContent = v;
  document.getElementById('revVRange').value = v;
}

async function applyRevHV() {
  const h = clampRevPct(+document.getElementById('revHVal').value);
  const v = clampRevPct(+document.getElementById('revVVal').value);
  state.rev_h = h;
  state.rev_v = v;
  pendingRevHV = { h, v };
  revHVDirtyUntil = Date.now() + 15000;
  document.getElementById('revHVal').value = h;
  document.getElementById('revHLabel').textContent = h;
  document.getElementById('revHRange').value = h;
  document.getElementById('revVVal').value = v;
  document.getElementById('revVLabel').textContent = v;
  document.getElementById('revVRange').value = v;
  const ok = await sendCmd('rev_hv ' + h + ' ' + v);
  if (!ok) {
    showBanner('rev_hv weak — holding H=' + h + ' V=' + v + ' until confirmed', true);
  }
  syncLabels();
}

async function setAxis(a) {
  const kind = isRevolveAxis(a) ? 'revolve' : 'rotate';
  await setFieldProgram(kind, a);
}

function clampGamma(v) {
  if (!Number.isFinite(v)) return 45;
  if (v < 0) v = 0;
  if (v > 90) v = 90;
  return v;
}

function onGammaInput(v) {
  v = clampGamma(v);
  state.gamma = v;
  gammaDirtyUntil = Date.now() + 15000;
  const el = document.getElementById('gammaVal');
  const lab = document.getElementById('gammaLabel');
  const rng = document.getElementById('gammaRange');
  if (el) el.value = v;
  if (lab) lab.textContent = (+v).toFixed(0);
  if (rng) rng.value = Math.round(v);
}

async function applyGamma() {
  const g = clampGamma(+document.getElementById('gammaVal').value);
  state.gamma = g;
  pendingGamma = g;
  gammaDirtyUntil = Date.now() + 15000;
  onGammaInput(g);
  const ok = await sendCmd('gamma ' + g);
  if (!ok) {
    showBanner('gamma weak — holding ' + g + ' until confirmed', true);
  }
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
  const axis = state.axis || 'y';
  const prog = state.program || (isRevolveAxis(axis) ? 'revolve' : 'rotate');
  document.getElementById('cfgAxisLabel').textContent = fieldCaption(prog, axis);
  document.getElementById('modeOpenBtn').classList.toggle('active', state.mode === 'open');
  document.getElementById('modeClosedBtn').classList.toggle('active', state.mode === 'closed');
  document.getElementById('dirCwBtn').classList.toggle('active', state.rot_dir === 'cw');
  document.getElementById('dirCcwBtn').classList.toggle('active', state.rot_dir === 'ccw');
  const rotY = document.getElementById('progRotYBtn');
  const revX = document.getElementById('progRevXBtn');
  const revZ = document.getElementById('progRevZBtn');
  const revT = document.getElementById('progRevTiltBtn');
  if (rotY) rotY.classList.toggle('active', prog === 'rotate');
  if (revX) revX.classList.toggle('active', prog === 'revolve' && axis === 'x');
  if (revZ) revZ.classList.toggle('active', prog === 'revolve' && axis === 'z');
  if (revT) revT.classList.toggle('active', prog === 'revolve' && axis === 'tilt');
  const cq = (state.park && state.corner >= 0) ? +state.corner : -1;
  for (let i = 0; i < 4; i++) {
    const b = document.getElementById('cornerQ' + i + 'Btn');
    if (b) b.classList.toggle('active', cq === i);
  }
  const cr = document.getElementById('cornerRunBtn');
  if (cr) cr.classList.toggle('active', cq < 0);
  const cl = document.getElementById('cornerLabel');
  if (cl) cl.textContent = cornerCaption(cq);

  const shownAmp = displayAmp();
  const stProg = document.getElementById('stProg');
  document.getElementById('stRot').textContent = state.rotate ? 'ON' : 'off';
  if (stProg) stProg.textContent = prog;
  document.getElementById('stAxis').textContent = axis;
  document.getElementById('stFreq').textContent = (+state.freq).toFixed(2);
  document.getElementById('stAmp').textContent = shownAmp;
  const stHV = document.getElementById('stHV');
  if (stHV) stHV.textContent = clampRevPct(state.rev_h) + '/' + clampRevPct(state.rev_v);
  const stG = document.getElementById('stGamma');
  if (stG) stG.textContent = clampGamma(state.gamma_live != null ? state.gamma_live : state.gamma).toFixed(0);
  document.getElementById('stDir').textContent = state.rot_dir;
  document.getElementById('stMode').textContent = state.mode;

  const ia = avgI([1, 3]);
  const ib = avgI([2, 4]);
  document.getElementById('stIA').textContent = ia.toFixed(3) + ' A';
  document.getElementById('stIB').textContent = ib.toFixed(3) + ' A';
  document.getElementById('phaseAI').textContent = ia.toFixed(3);
  document.getElementById('phaseBI').textContent = ib.toFixed(3);

  const paTitle = document.getElementById('phaseATitle');
  const pbTitle = document.getElementById('phaseBTitle');
  const paRole = document.getElementById('phaseARole');
  const pbRole = document.getElementById('phaseBRole');
  if (paTitle && pbTitle && paRole && pbRole) {
    if (prog === 'revolve' && axis === 'x') {
      paTitle.textContent = 'M1 / M3 · YZ (H)';
      pbTitle.textContent = 'M2 / M4 · extra By';
      paRole.textContent = 'Revolve X · YZ, γ=0';
      pbRole.textContent = 'all four coils, 30° inverse';
    } else if (prog === 'revolve' && axis === 'z') {
      paTitle.textContent = 'M2 / M4 · XY (H)';
      pbTitle.textContent = 'M1 / M3 · extra By';
      paRole.textContent = 'Revolve Z · XY, γ=90';
      pbRole.textContent = 'all four coils, 30° inverse';
    } else if (prog === 'revolve' && axis === 'tilt') {
      paTitle.textContent = 'M1 / M3 · YZ pair';
      pbTitle.textContent = 'M2 / M4 · XY pair';
      paRole.textContent = 'Revolve tilt · γ=' + clampGamma(state.gamma).toFixed(0) + '°';
      pbRole.textContent = 'all four coils, 30° equal-V split';
    } else {
      paTitle.textContent = 'M1 ∥ M3 · cos ωt';
      pbTitle.textContent = 'M2 ∥ M4 · sin ωt';
      paRole.textContent = 'Rotate Y · XZ';
      pbRole.textContent = 'same current direction in each pair';
    }
  }

  const hint = document.getElementById('plotRunHint');
  if (hint) {
    const cap = fieldCaption(liveProgram(), liveAxis());
    if (!state.rotate) hint.textContent = 'field off → flat';
    else if (!(+shownAmp > 0) && !(+state.amp > 0)) hint.textContent = cap + ' ON but amp=0 → set Amplitude to see waves';
    else {
      const derated = state.amp_live != null && +state.amp_live < +state.amp;
      const flipping = state.rot_dir_live && state.rot_dir && state.rot_dir_live !== state.rot_dir;
      const progFlip = state.program_live && state.program && state.program_live !== state.program;
      const axisFlip = state.axis_live && state.axis && state.axis_live !== state.axis;
      const ramping = derated || flipping || progFlip || axisFlip || (+shownAmp !== +state.amp);
      const hv = (liveProgram() === 'revolve')
        ? (` · H/V ${clampRevPct(state.rev_h)}/${clampRevPct(state.rev_v)}` +
           ` · γ ${clampGamma(liveAxis() === 'x' ? 0 : liveAxis() === 'z' ? 90 : state.gamma).toFixed(0)}°`)
        : '';
      hint.textContent = `${cap} · ${(+state.freq).toFixed(2)} Hz · amp ${shownAmp}` + hv +
        (derated || ramping ? ` (cmd ${state.amp})` : '') +
        ` · ${flipping ? state.rot_dir_live + '→' + state.rot_dir : state.rot_dir}` +
        (ramping ? ' · ramping' : '');
    }
  }

  const freqEl = document.getElementById('freqVal');
  const ampEl = document.getElementById('ampVal');
  const revHEl = document.getElementById('revHVal');
  const revVEl = document.getElementById('revVVal');
  const freqBusy = pendingFreq != null || Date.now() < freqDirtyUntil ||
    document.activeElement === freqEl ||
    document.activeElement === document.getElementById('freqRange');
  const ampBusy = pendingAmp != null || Date.now() < ampDirtyUntil ||
    document.activeElement === ampEl ||
    document.activeElement === document.getElementById('ampRange');
  const revBusy = pendingRevHV != null || Date.now() < revHVDirtyUntil ||
    document.activeElement === revHEl ||
    document.activeElement === revVEl ||
    document.activeElement === document.getElementById('revHRange') ||
    document.activeElement === document.getElementById('revVRange');
  if (!freqBusy) {
    freqEl.value = (+state.freq).toFixed(2);
    document.getElementById('freqLabel').textContent = (+state.freq).toFixed(2);
    document.getElementById('freqRange').value = Math.round((+state.freq) * 100);
  } else if (pendingFreq != null) {
    freqEl.value = (+pendingFreq).toFixed(2);
    document.getElementById('freqLabel').textContent = (+pendingFreq).toFixed(2);
    document.getElementById('freqRange').value = Math.round((+pendingFreq) * 100);
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
  if (revHEl && revVEl) {
    if (!revBusy) {
      const h = clampRevPct(state.rev_h);
      const v = clampRevPct(state.rev_v);
      revHEl.value = h;
      document.getElementById('revHLabel').textContent = h;
      document.getElementById('revHRange').value = h;
      revVEl.value = v;
      document.getElementById('revVLabel').textContent = v;
      document.getElementById('revVRange').value = v;
    } else if (pendingRevHV) {
      revHEl.value = pendingRevHV.h;
      document.getElementById('revHLabel').textContent = pendingRevHV.h;
      document.getElementById('revHRange').value = pendingRevHV.h;
      revVEl.value = pendingRevHV.v;
      document.getElementById('revVLabel').textContent = pendingRevHV.v;
      document.getElementById('revVRange').value = pendingRevHV.v;
    }
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
      if (Math.abs(f - pendingFreq) < 0.005) {
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

  const revBusy = pendingRevHV != null || Date.now() < revHVDirtyUntil ||
    document.activeElement === document.getElementById('revHVal') ||
    document.activeElement === document.getElementById('revVVal') ||
    document.activeElement === document.getElementById('revHRange') ||
    document.activeElement === document.getElementById('revVRange');
  if (s.rev_h != null && Number.isFinite(+s.rev_h) && s.rev_v != null && Number.isFinite(+s.rev_v)) {
    const h = clampRevPct(+s.rev_h);
    const v = clampRevPct(+s.rev_v);
    if (pendingRevHV != null) {
      if (Math.abs(h - pendingRevHV.h) < 0.5 && Math.abs(v - pendingRevHV.v) < 0.5) {
        state.rev_h = h;
        state.rev_v = v;
        pendingRevHV = null;
        revHVDirtyUntil = 0;
      } else {
        state.rev_h = pendingRevHV.h;
        state.rev_v = pendingRevHV.v;
        scheduleResync();
      }
    } else if (!revBusy) {
      state.rev_h = h;
      state.rev_v = v;
    }
  }

  if (s.corner != null && Number.isFinite(+s.corner)) state.corner = +s.corner;
  if (s.park != null) state.park = +s.park ? 1 : 0;
  if (s.travel != null) state.travel = +s.travel ? 1 : 0;
  if (s.rot_dir) state.rot_dir = s.rot_dir;
  if (s.rot_dir_live) state.rot_dir_live = s.rot_dir_live;
  else if (s.rot_dir) state.rot_dir_live = s.rot_dir;
  if (s.program) state.program = s.program;
  if (s.program_live) state.program_live = s.program_live;
  else if (s.program) state.program_live = s.program;
  if (s.axis) state.axis = s.axis;
  if (s.axis_live) state.axis_live = s.axis_live;
  else if (s.axis) state.axis_live = s.axis;
  if (!state.program) {
    state.program = isRevolveAxis(state.axis) ? 'revolve' : 'rotate';
  }
  if (!state.program_live) {
    state.program_live = isRevolveAxis(state.axis_live) ? 'revolve' : 'rotate';
  }
  if (s.gamma != null && Number.isFinite(+s.gamma)) {
    const gBusy = pendingGamma != null || Date.now() < gammaDirtyUntil;
    const g = clampGamma(+s.gamma);
    if (pendingGamma != null && Math.abs(g - pendingGamma) < 0.51) {
      pendingGamma = null;
      gammaDirtyUntil = 0;
      state.gamma = g;
    } else if (!gBusy) {
      state.gamma = g;
      const el = document.getElementById('gammaVal');
      const lab = document.getElementById('gammaLabel');
      const rng = document.getElementById('gammaRange');
      if (el) el.value = g;
      if (lab) lab.textContent = g.toFixed(0);
      if (rng) rng.value = Math.round(g);
    }
  }
  if (s.gamma_live != null && Number.isFinite(+s.gamma_live)) state.gamma_live = +s.gamma_live;
  if (s.tilt_scale != null && Number.isFinite(+s.tilt_scale)) state.tilt_scale = +s.tilt_scale;
  if (Array.isArray(s.signed) && s.signed.length >= 4) {
    state.signed = s.signed.slice(0, 4).map(v => +v || 0);
  }
  if (Array.isArray(s.pr) && s.pr.length >= 4) {
    state.pr = s.pr.slice(0, 4).map(v => +v || 0);
  }
  if (Array.isArray(s.pl) && s.pl.length >= 4) {
    state.pl = s.pl.slice(0, 4).map(v => +v || 0);
  }
  const snapEl = document.getElementById('signedSnap');
  if (snapEl) {
    const sd = state.signed || [0, 0, 0, 0];
    const pr = state.pr || [0, 0, 0, 0];
    const pl = state.pl || [0, 0, 0, 0];
    snapEl.textContent =
      `M1..M4 signed=[${sd.join(',')}]  RPWM=[${pr.join(',')}]  LPWM=[${pl.join(',')}]`;
  }
  if (s.mlx && typeof s.mlx === 'object') state.mlx = s.mlx;
  const mlx = state.mlx;
  const mlxSnap = document.getElementById('mlxSnap');
  const mlxPresent = document.getElementById('mlxPresent');
  const mlxB = document.getElementById('mlxB');
  const mlxCoils = document.getElementById('mlxCoils');
  if (mlx && +mlx.ok) {
    const ctr = mlx.center || mlx;
    const btxt = `Bx ${(+ctr.bx).toFixed(2)}  By ${(+ctr.by).toFixed(2)}  Bz ${(+ctr.bz).toFixed(2)}  |B| ${(+ctr.babs).toFixed(2)} mT` +
      (+mlx.sat ? '  SAT' : '');
    if (mlxSnap) mlxSnap.textContent = btxt;
    const n = (mlx.n != null) ? +mlx.n : 1;
    if (mlxPresent) mlxPresent.textContent = n + '/4';
    if (mlxB) mlxB.textContent = btxt;
    if (mlxCoils && Array.isArray(mlx.coils)) {
      mlxCoils.textContent = mlx.coils.map((c, i) =>
        +c.ok ? ('M' + (i + 1) + ' 0x' + ((+c.addr) >>> 0).toString(16) +
          ' |B| ' + (+c.babs).toFixed(2)) : ('M' + (i + 1) + ' --')).join(' · ');
    } else if (mlxCoils) {
      mlxCoils.textContent = '—';
    }
  } else {
    if (mlxSnap) mlxSnap.textContent = 'not found on I2C2';
    if (mlxPresent) mlxPresent.textContent = '0/4';
    if (mlxB) mlxB.textContent = '—';
    if (mlxCoils) mlxCoils.textContent = '—';
  }
  s.coils.forEach((c, idx) => {
    if (!state.coils[idx]) return;
    Object.assign(state.coils[idx], c);
  });
  // #region agent log
  const _now = Date.now();
  if (_now - _dbgLastStatus > 800) {
    _dbgLastStatus = _now;
    dbgLog('H1H2H3', 'www_page.h:applyStatus', 'status snapshot', {
      rotate:s.rotate, program:s.program, program_live:s.program_live,
      axis:s.axis, axis_live:s.axis_live,
      amp:s.amp, amp_live:s.amp_live, rev_h:s.rev_h, rev_v:s.rev_v, dbg:s.dbg || null,
      coils:(s.coils||[]).map(c => ({id:c.id, en:c.en, mag:c.mag, duty:c.duty, i:c.i, lim:c.lim}))
    });
  }
  // #endregion
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
      if (pendingRevHV != null) {
        await sendCmd('rev_hv ' + pendingRevHV.h + ' ' + pendingRevHV.v);
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
      `linked · ${state.rotate ? ((liveProgram() === 'revolve') ? 'REV' : 'ROT') : 'DC'} · ${state.mode}`;
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
          <h2>Coil M${c.id}${idx % 2 === 0 ? ' · Z' : ' · X'}</h2>
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
      // #region agent log
      dbgLog('H4', 'www_page.h:poll', 'uart up status stale', {uart_ok:j.uart_ok, raw:(j.raw||'').toString().slice(0,120)});
      // #endregion
      setLink(false, 'uart_only');
    } else {
      // #region agent log
      dbgLog('H4', 'www_page.h:poll', 'status unlinked', {uart_ok:j.uart_ok, raw:(j.raw||'').toString().slice(0,120)});
      // #endregion
      setLink(false, 'status_unlinked');
    }
    syncLabels();
  } catch (e) {
    setLink(false, 'poll_fetch');
  } finally {
    _pollBusy = false;
  }
}

function setPlotMode(mode) {
  WAVE.mode = (mode === 'unipolar') ? 'unipolar' : 'bipolar';
  const bi = document.getElementById('plotBipolarBtn');
  const uni = document.getElementById('plotUnipolarBtn');
  if (bi) bi.classList.toggle('active', WAVE.mode === 'bipolar');
  if (uni) uni.classList.toggle('active', WAVE.mode === 'unipolar');
  const yLab = document.getElementById('plotYLabel');
  const src = document.getElementById('plotSource');
  if (yLab) {
    yLab.innerHTML = WAVE.mode === 'unipolar'
      ? 'y: RPWM (+)/LPWM (−) <b>0..255</b>'
      : 'y: signed duty <b>±255</b>';
  }
  if (src) src.textContent = WAVE.mode === 'unipolar'
    ? 'math preview → RPWM/LPWM'
    : 'math preview (bipolar cmd)';
  buildPlotLegend();
}

function buildPlotLegend() {
  const root = document.getElementById('plotLegend');
  if (!root) return;
  root.innerHTML = '';
  waveSeries().forEach((s, i) => {
    const lab = document.createElement('label');
    lab.className = s.visible ? 'on' : 'off';
    lab.innerHTML =
      `<input type="checkbox" ${s.visible ? 'checked' : ''} onchange="toggleWave(${i}, this.checked)">` +
      `<span class="swatch" style="background:${s.color}"></span>${s.id}`;
    root.appendChild(lab);
  });
}

function toggleWave(i, on) {
  const series = waveSeries();
  if (!series[i]) return;
  series[i].visible = !!on;
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

const FIELD_CH = Math.sqrt(3) / 2;
const FIELD_INV_H = 1 / (2 * FIELD_CH);
const FIELD_INV_V = 1 / (4 * 0.5);

function tiltCommandsAt(tSec, ampH, ampV, gammaDeg, ccw) {
  const f = Math.max(0.01, +state.freq || 0);
  let theta = 2 * Math.PI * f * tSec;
  if (ccw) theta = -theta;
  const r = clampGamma(gammaDeg) * Math.PI / 180;
  let sg = Math.sin(r), cg = Math.cos(r);
  if (gammaDeg <= 0) { sg = 0; cg = 1; }
  if (gammaDeg >= 90) { sg = 1; cg = 0; }
  const p13 = Math.hypot(ampH * cg * FIELD_INV_H, ampV * FIELD_INV_V);
  const p24 = Math.hypot(ampH * sg * FIELD_INV_H, ampV * FIELD_INV_V);
  const pmax = Math.max(p13, p24);
  const s = (pmax > 255) ? (255 / pmax) : 1;
  const H = (ampH * s) * Math.cos(theta);
  const V = (ampV * s) * Math.sin(theta);
  return [
    -H * cg * FIELD_INV_H + V * FIELD_INV_V,
     H * sg * FIELD_INV_H + V * FIELD_INV_V,
    -H * cg * FIELD_INV_H - V * FIELD_INV_V,
     H * sg * FIELD_INV_H - V * FIELD_INV_V
  ];
}

function coilDutyAt(tSec, coilId) {
  /* Bipolar signed-command preview when the AC field is on (amp=0 → flat). */
  if (!state.rotate) return 0;
  const f = Math.max(0.01, +state.freq || 0);
  const amp = Math.max(0, Math.min(255, +displayAmp() || 0));
  const theta = 2 * Math.PI * f * tSec;
  const ccw = (state.rot_dir_live || state.rot_dir) === 'ccw';
  const prog = liveProgram();
  const axis = liveAxis();
  if (prog !== 'revolve') {
    if (coilId === 1 || coilId === 3) return amp * Math.cos(theta);
    const s = amp * Math.sin(theta);
    return ccw ? -s : s;
  }
  const Ah = amp * clampRevPct(state.rev_h) / 100;
  const Av = amp * clampRevPct(state.rev_v) / 100;
  let gammaDeg = clampGamma(state.gamma);
  if (axis === 'x') gammaDeg = 0;
  else if (axis === 'z') gammaDeg = 90;
  let t = tSec;
  if (state.park && state.corner >= 0) {
    t = ((+state.corner) / 4) / f;
    const raw = tiltCommandsAt(t, Ah, Av, gammaDeg, false);
    return raw[coilId - 1] || 0;
  }
  const raw = tiltCommandsAt(t, Ah, Av, gammaDeg, ccw);
  return raw[coilId - 1] || 0;
}

/* Unipolar pin duties from bipolar cmd: +duty → RPWM, −duty → LPWM. */
function coilLegDutyAt(tSec, coilId, leg) {
  const signed = coilDutyAt(tSec, coilId);
  if (leg === 'lpwm') return signed < 0 ? -signed : 0;
  return signed > 0 ? signed : 0;
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
  const unipolar = WAVE.mode === 'unipolar';
  /* Unipolar: plot +RPWM above zero and −LPWM below so legs don't overlap. */
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
  if (unipolar) {
    ctx.fillText('+RPWM', padL - 6, padT + 10);
    ctx.fillText('0', padL - 6, yOf(0) + 3);
    ctx.fillText('−LPWM', padL - 6, padT + plotH);
  } else {
    ctx.fillText('+255', padL - 6, padT + 10);
    ctx.fillText('0', padL - 6, yOf(0) + 3);
    ctx.fillText('-255', padL - 6, padT + plotH);
  }
  ctx.textAlign = 'center';
  ctx.fillText((-tWin).toFixed(1) + 's', padL, H - 6);
  ctx.fillText('0', padL + plotW, H - 6);

  /* Traces — denser samples for smooth Teleplot look */
  const steps = Math.min(900, Math.max(200, Math.floor(plotW)));
  waveSeries().forEach(s => {
    if (!s.visible) return;
    ctx.strokeStyle = s.color;
    ctx.lineWidth = unipolar ? 1.2 : 1.6;
    ctx.beginPath();
    for (let i = 0; i <= steps; i++) {
      const t = tStart + (tWin * i) / steps;
      let v;
      if (unipolar) {
        const mag = coilLegDutyAt(t, s.coil, s.leg);
        v = (s.leg === 'lpwm') ? -mag : mag;
      } else {
        v = coilDutyAt(t, s.coil);
      }
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

  const mlx = state.mlx;
  if (mlx && +mlx.ok) {
    const ctr = mlx.center || mlx;
    ctx.fillStyle = '#9ecbff';
    ctx.font = '11px ui-monospace, Menlo, monospace';
    ctx.textAlign = 'left';
    ctx.fillText(
      'centre |B| ' + (+ctr.babs).toFixed(2) + ' mT  Bx ' + (+ctr.bx).toFixed(2) +
        '  By ' + (+ctr.by).toFixed(2) + '  Bz ' + (+ctr.bz).toFixed(2),
      padL + 8, padT + 14);
  }
}

function waveLoop() {
  drawWavePlot();
  WAVE.raf = requestAnimationFrame(waveLoop);
}

showTab('field');
render(true);
syncLabels();
setPlotMode('bipolar');
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
