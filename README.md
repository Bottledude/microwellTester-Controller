# Soft Magnetic Composite Controller

PlatformIO firmware for driving **four** soft-magnetic coils with one **ESP32-WROOM-32**, a **PCA9685** PWM expander, and **BTS7960 / IBT-2** half-bridge drivers. The same chip serves a WiFi SoftAP lab UI and pairs a **DualSense** over Bluetooth Classic.

**Repo:** https://github.com/Bottledude/microwellTester-Controller

This firmware is **four coils only** (M1–M4). PWM goes out the PCA9685; **EN is ESP32 GPIO**. There is **no current sense**. Duty is open-loop 0–255.

## Architecture

```text
    Browser  ──WiFi SoftAP──►  DualSense (BT Classic)
                              │
                              ▼
                         ESP32-WROOM-32
              ┌───────────────┼────────────────┐
         I2C0 GPIO21/22   I2C1 GPIO18/19    EN GPIO
              │               │                │
           PCA9685        4× MLX90393          │
           PWM ch0–7      (behind M1–M4)       │
              │                                │
              └───────────────┬────────────────┘
                              ▼
                   BTS7960 ×4 → coils
```

| Role | Board | Firmware |
|------|--------|----------|
| Coil driver / CLI / web / pad | ESP32-WROOM-32 DevKit | root `platformio.ini` (`esp32dev`) |

UART0 USB is the Serial Monitor CLI and optional Teleplot port.

## Features

- **4 coils** at **90°** around the sample, each on its own BTS7960 (RPWM / LPWM / EN)
- Lab frame: **Y is vertical** (out of plane); **X and Z are horizontal**. Coils are **tilted 30° from horizontal** (`c_h=√3/2`, `c_v=1/2`) so each contributes both horizontal and vertical field at the centre
- **PCA9685 PWM carrier ~1 kHz** (chip max ~1.5 kHz), duty **0–255** mapped to 12-bit
- **DC mode:** constant magnitude + direction per coil (open-loop)
- **Rotate Y** (field in the **XZ** plane, all four coils)
  - M1 = M3 ∝ cos ωt, M2 = M4 ∝ sin ωt
  - Each pair shares the **same current direction**; the two pairs are 90° apart
  - CW: M2∥M4 = +sin; CCW: M2∥M4 = −sin
- **Revolve X / Z / tilt** share one **four-coil 30° law** (`field_tilt.c`). Every coil gets a pure sinusoid, so the field turns smoothly at constant |B| for any γ
  - Each coil at duty u gives \(c_v u\) vertical and \(c_h u\) horizontal. Within a pair, same-sign duties add horizontal and cancel vertical; opposite-sign duties add vertical and cancel horizontal
  - Target \(B=H\hat h+V\hat y\), \(H=A_h\cos\theta\), \(V=A_v\sin\theta\). Per coil:
    \(u_1=-a_{13}H+w_{13}V\), \(u_3=-a_{13}H-w_{13}V\), \(u_2=a_{24}H+w_{24}V\), \(u_4=a_{24}H-w_{24}V\)
  - Horizontal: \(a_{13}=\cos\gamma/(2c_h)\), \(a_{24}=\sin\gamma/(2c_h)\). Vertical: \(w_{13}=\tfrac12-\tfrac{\cos2\gamma}{6}\), \(w_{24}=\tfrac12+\tfrac{\cos2\gamma}{6}\) (they sum to \(1/(2c_v)\))
  - Revolve X (γ=0): M1/M3 make H plus 1/3 of V; M2/M4 make 2/3 of V and no H. Revolve Z (γ=90) is the reverse. γ=45: both pairs carry half of each
  - The vertical split is chosen so both pairs need the same peak duty. At `amp 255` (rev_hv 100/100) all four coils peak at ±255 together, which is the largest constant-|B| circle with sinusoidal drive: \(|B|=\mathrm{amp}\cdot g\), \(g=6/\sqrt{15+\cos^2 2\gamma}\) (1.5 at γ=0/90/180, 1.549 at γ=45/135). `status` / `json` report \(g\) as `rev_gain`
  - Why not \(2c_h\cdot255=\sqrt3\cdot255\)? That is the horizontal pair's limit when it spends all its duty on H. Mid-revolution M1/M3 must carry H and part of V at once, and with sine drive those add in quadrature, so the circle tops out at \(1.5\cdot255=382.5\) for X/Z (up from 255 with the old equal split)
  - Revolve X: \(\gamma=0\) (YZ plane). Revolve Z: \(\gamma=90\) (XY plane, Q0 is **−X**). Tilt: live \(\gamma\) slider, 0–180° (every axis in the XZ plane)
  - \(A_h=\mathrm{amp}\cdot g\cdot h/100\), \(A_v=\mathrm{amp}\cdot g\cdot v/100\) (`rev_hv`; 100/100 is a PWM-proxy circle). Unequal radii are an ellipse
  - Forward: \(B_x=-c_h(u_2+u_4)\), \(B_y=c_v(u_1+u_2-u_3-u_4)\), \(B_z=-c_h(u_1+u_3)\)
  - Changing `gamma` while running ramps live amp through 0, then commits \(\sin\gamma/\cos\gamma\) together
- **Revolve A scale:** `rev_hv` remains an empirical H/V trim (1–200%), relative to the max circle. Rotate Y ignores this. Above 100% the ellipse no longer fits, so all four coils share one scale (`tilt_scale` < 1)
- **4× MLX90393** behind M1–M4 (I2C1) reconstruct centre \(B\) via coil-axis projection + the same 30° forward map. Display / Teleplot / JSON by default; `fb on` closes the loop (see [MLX feedback](#mlx-feedback-fb))
- Shared **frequency** (0.01–50 Hz, default 1 Hz) and **amplitude** (0–255, **default 0** — set `amp` before START). Sine LUT is **360** samples (1°).
- ~1 kHz sine sample; I2C pushes the latest duty to the PCA from a core-1 flush task
- **Amp ramp** (~1.3 s for 0↔255) on start, stop, amp changes, CW↔CCW, and rotate↔revolve. Revolve start/stop **freezes phase** and maps that ramp through a quarter-sine envelope so |A| swells/shrinks smoothly at a cardinal.
- Lab web UI tabs: **Field**, **DC coils**, **Logs** (sticky **ALL OFF**)
- DualSense over Bluetooth Classic (Bluepad32)

## Hardware

Board is a classic **ESP32-WROOM-32 DevKit** (`esp32dev`). An ESP32-S3 will not pair a DualSense (no Bluetooth Classic). ESP32 pins are **not 5 V tolerant** — both I2C buses stay at 3.3 V.

### PCA9685 PWM + ESP32 EN

RPWM/LPWM go to the **PCA9685**, not the ESP32 LEDC timers. EN is GPIO. There is no IS / ADC wiring.

**I2C (ESP32 → PCA9685)**

| Signal | ESP32 | PCA9685 |
|--------|-------|---------|
| SCL | **GPIO22** (I2C0) | SCL |
| SDA | **GPIO21** (I2C0) | SDA |
| GND | GND | GND |
| — | — | **OE → GND** (outputs enabled) |
| — | — | VCC = 5 V (3.3 V I2C is OK on Adafruit-style boards) |

Default 7-bit address **0x40** (A0–A5 = 0). Override with `-DPCA9685_I2C_ADDR=0x41` if you strapped A0.

| Coil | Place | PCA RPWM | PCA LPWM | EN (ESP32) |
|------|-------|----------|----------|------------|
| M1 | YZ pair (revolve x / rotate-Y cos) | ch0 | ch1 | **GPIO25** |
| M2 | XY pair (revolve z / rotate-Y sin) | ch2 | ch3 | **GPIO26** |
| M3 | YZ pair (revolve x / rotate-Y cos) | ch4 | ch5 | **GPIO27** |
| M4 | XY pair (revolve z / rotate-Y sin) | ch6 | ch7 | **GPIO14** |

The PCA carrier is ~1 kHz. Rotate / revolve still work; the sine is updated at 1 kHz in RAM and pushed over I2C from the flush task.

IBT-2 / BTS7960: tie **R_EN + L_EN** to the ESP32 EN pin, logic **VCC = 5 V**, **VS** = coil supply. EN is 3.3 V. Most IBT-2 boards accept that. If one does not, drive that EN from an unused PCA9685 channel (8–15) held fully on or fully off.

### MLX90393 behind each coil

Four chips share **I2C1**. Mount one behind each coil (not at the sample centre). Unique 7-bit addresses via A0/A1/CS straps. **Do not share I2C0** with the PCA9685.

**I2C (ESP32 → MLX90393 ×4)**

| Signal | ESP32 | MLX90393 / breakout |
|--------|-------|---------------------|
| SCL | **GPIO19** (I2C1) | SCL (bus) |
| SDA | **GPIO18** (I2C1) | SDA (bus) |
| VIN / VDD | **3.3 V** | 2.2–3.6 V |
| GND | GND | GND |
| CS / SENB | — | **3.3 V** (I2C mode) |
| A0, A1 | — | strap uniquely per coil |
| INT | unused | firmware polls |

Add **4.7 kΩ** pull-ups to 3.3 V on SCL/SDA if the boards do not already have them.

`mlx scan` probes `0x0C`–`0x1B` and binds sorted addresses to M1–M4. Override with `mlx bind <a1> <a2> <a3> <a4>` (`0` = auto for that slot). Reconstruction scale: `mlx k <M#|all> <s>` (default 1). `mlx offset` takes Earth/bias \(B_0\) with coils off.

**Sensor frame.** Each chip sits on its coil's axis, as far behind the back face as the centre is in front of the front face. Its frame is **z along the coil axis (out of the chip plane), x up, y left** (right-handed, looking at the chip face). The chip tilts 30° with the coil, so "up" is lab +Y with the axial part removed. `mlx map [M#] x y z` remaps raw chip axes into that frame if a breakout is rotated (default identity). `mlx zsign <M#|all> <+|->` says whether chip +z points along (+) or against (−) the field that coil makes at the centre for **positive** duty (\(\hat{a}_i\) below). Default is `+` on all four.

Check `zsign` per coil: run `mlx offset`, drive only that coil with a positive DC duty, and read `mlx`. That coil's `axial` value must be positive. If it is negative, flip it with `mlx zsign M# -`. If all four chips face the same way relative to their own coils, expect one coil of each pair (M1/M3, M2/M4) to need `-`.

**Centre field.** The chip is the mirror image of the centre through the coil's midplane, and a coil's on-axis field is symmetric about that plane. So the chip's axial reading equals that coil's field at the centre: \(u_i=\mathrm{zsign}_i\,B_{z,i}/k_i\) (missing chips → 0), then \(B_c=\sum_i u_i\hat{a}_i\) (the 30° forward map). Chip x/y are not used for the centre. On axis a coil's own transverse field is 0, so nonzero x/y means misalignment or field from the other coils. Per-coil lab vectors (`mlx`, `json` `coils[].bx/by/bz`, Teleplot `M*_b*_m`) are the chip reading rotated into lab. `json` also carries `coils[].zs` and the chip-frame `sx/sy/sz`. `k_i≠1` corrects for a chip that is not exactly at the mirror distance.

| Coil | \(\hat{a}\) (lab) |
|------|-------------------|
| M1 | \((0,\ c_v,\ -c_h)\) |
| M2 | \((-c_h,\ c_v,\ 0)\) |
| M3 | \((0,\ -c_v,\ -c_h)\) |
| M4 | \((-c_h,\ -c_v,\ 0)\) |

Range boots at analog 1× + 19-bit (~±39 mT XY). If a chip reports SAT, lower `amp`. Hall data only feeds PWM when `fb on`.

## Build & flash

Requires [PlatformIO](https://platformio.org/) (prefer `~/.platformio/penv/bin/pio` over distro packages).

```bash
cd /path/to/microwellTester-Controller
pio run -t upload
pio device monitor   # 115200, UART0 USB
```

Board env: `esp32dev` (Arduino-ESP32 2.x via `espressif32 @ 6.10.0`). Useful `build_flags` in `platformio.ini`: `PWM_TARGET_HZ` (PCA carrier, clamp 24–1526 Hz). Optional `-DROTATE_RAMP_STEP=N` (live-amp LSB per 200 Hz tick; default `1` ≈ 1.3 s for 0↔255). `NUM_COILS` must stay **4**.

WiFi SSID/password and pins: `include/config.h`.

## Web UI

1. Join SoftAP **`CoilControl`** / **`coilcontrol`**
2. Open **http://192.168.4.1/**
3. Tabs:
   - **Field** — Rotate Y / Revolve X / Revolve Z / Revolve tilt, START/STOP, freq/amp, γ, CW/CCW, MLX scan/offset. Canvas is a **math preview** of the commanded waveform (bipolar signed duty, or unipolar RPWM/LPWM). Last `json` sample shows live `signed` / `pr` / `pl` and reconstructed `mlx.center` mT. For a live scope, use Teleplot on UART0.
   - **DC coils** — per-coil constant drive, contour, program P1/P2. All four lock while the AC field is on (STOP first).
   - **Logs** — command traffic + raw CLI
4. Field **STOP** sends `rotate off` (amp fades; also stops revolve). Header **ALL OFF** sends `off all` only — it **cuts immediately** and also stops the AC field.
5. Link pill is **linked** after a parsed `json` coil snapshot.

## Serial CLI (UART0)

UART0 at 115200 accepts the same commands as the web UI (`help` for the full list). Motors are **M1–M4** only. Incomplete lines print usage; unknown motor tokens print an error. Examples:

```text
help
ping
json
status
on M1 M2
off all
set M1 180
dir M1 +
drive M2 200 -
contour on|off
program P1|P2|stop
axis y
axis x
axis z
axis tilt
rotate on|off
rotate on 2.5 160
revolve z [Hz] [0..255]
revolve x [Hz] [0..255]
revolve tilt [Hz] [0..255]
revolve off
gamma 45
rev_hv 100 80
freq 5
amp 180
rot_dir cw|ccw
corner Q0|Q1|Q2|Q3|run
teleplot on|off
mlx
mlx on|off
mlx scan
mlx bind 0x0C 0x0D 0x0E 0x0F
mlx map [M#] x y z
mlx k all 1
mlx offset
```

### DualSense pad (Bluetooth Classic → ESP32)

The DualSense pairs to the **ESP32** over Bluetooth Classic. A USB cable to the pad will not work (no USB host). Put the pad in pairing mode; Bluepad32 accepts it. WiFi and Classic Bluetooth share one radio, so the web UI may hitch while the pad is connected. Coil PWM stays on core 1.

Optional laptop mapper (UART0 fallback if you are not using onboard BT):

```text
pip install -r monitor/requirements-gamepad.txt
python monitor/dualsense_field.py --list
python monitor/dualsense_field.py --port COM9
python monitor/dualsense_field.py --dry-run
```

That process **owns UART0**. Close `pio device monitor` and Teleplot on that port first.

| Pad | CLI |
|-----|-----|
| D-pad Up / Left / Right / Down | `axis y` / `x` / `z` / `tilt` |
| Options | START (`rotate on` or `revolve <axis>`) |
| Create | STOP (`rotate off`) |
| PS | `off all` (immediate) |
| L1 / R1 | `rot_dir ccw` / `cw` |
| □ / ✕ / ○ / △ | `corner Q0` / `Q1` / `Q2` / `Q3` |
| Left stick X | `gamma 0..180` (5° steps; rest holds) |
| Right stick X | `amp 0..255` (rest holds) |
| Right stick Y | slew `freq` |
| Touchpad / mute | `status` |

Changing `gamma` / program while the field is on still ramps amp through 0.

### Waveform observe (Teleplot)

Install the [Teleplot](https://marketplace.visualstudio.com/items?itemName=alexnesnes.teleplot) extension (`alexnesnes.teleplot`). Then connect to the ESP32 UART0 port at **115200**.

Live commanded duties (bipolar `signed` and unipolar `RPWM`/`LPWM`) stream on **UART0** at ~50 Hz when enabled. Off by default so UART does not starve PCA9685 flushes.

```text
pio device monitor
teleplot on
```

Coil PWM series: `M1_signed`…`M4_signed`, `M*_rpwm`, `M*_lpwm`, plus `amp_live`.

Lab **B** (PWM-duty proxy, not Tesla — signed duty is treated as coil current) uses the same 30° forward map as `field_tilt_forward`:

\[
B_x=-c_h(u_2+u_4),\quad
B_y=c_v(u_1+u_2-u_3-u_4),\quad
B_z=-c_h(u_1+u_3)
\]

with \(c_h=\sqrt{3}/2\), \(c_v=1/2\).

- Time: `Bx` (XY horizontal, M2/M4), `By` (vertical, both pairs), `Bz` (YZ horizontal, M1/M3), `Babs` \(=\sqrt{B_x^2+B_y^2+B_z^2}\)
- XY plots: `Bxy` (Bx vs By), `Bxz` (Bz vs Bx), `Byz` (Bz vs By)

Rotate Y: `By≈0`, `Bx`/`Bz` in quadrature, `Babs` nearly flat at ~amp√3 (circle on `Bxz`). Revolve X/Z/tilt: `Babs` stays at amp·`rev_gain` (1.5·amp for X/Z); tilt adds `Bn≈0` plus `gamma`, `Bh`, `Bv`, `Bn`, `tilt_scale`.

**Reconstructed centre MLX** (mT): `Bx_c` / `By_c` / `Bz_c` / `Babs_c` (aliases `Bx_m`…). XY plots `Bxy_m` / `Bxz_m` / `Byz_m`. Per-coil lab vectors: `M1_bx_m`…. Expected centre field (mT, same phase as the chip set, available once feedback has an estimate): `Bx_e` / `By_e` / `Bz_e` / `Babs_e`, XY plots `Bxy_e` / `Byz_e` / `Bxz_e`, and `fb_err` (% circle error). Overlay `Byz_m` on `Byz_e` to see the measured vs expected circle.

Do not expect the PCA ~1 kHz carrier over serial — use a scope for that.

```text
teleplot off
```

### Rotate / revolve

**Rotate Y** (XZ plane, all four coils):

```text
freq 2.5
amp 160
rot_dir cw
rotate on
```

**Revolve Z** (XY plane, four-coil synth at \(\gamma=90\)):

```text
rev_hv 100 100
revolve z 2.5 160
```

**Revolve X** (YZ plane, four-coil synth at \(\gamma=0\)):

```text
revolve x 2.5 160
```

**Revolve tilt** (fixed XZ-plane axis, all four coils). Default \(\gamma=45^\circ\):

```text
gamma 45
rev_hv 100 100
revolve tilt 0.5 80
```

`gamma 0` is the YZ plane (axis +X). `gamma 90` is the XY plane (axis +Z). `gamma 135` is the axis between +Z and −X. `gamma 180` is the YZ plane again (axis −X, so the rotation runs the other way). Out-of-range values (`gamma 181`, `gamma -1`) are rejected and leave state unchanged. Unequal `rev_hv` in this mode is an ellipse. If a coil would exceed ±255, all four commands share one scale (`tilt_scale` in `status` / `json` / Teleplot).

Park at a cardinal with `corner Q0` … `Q3` (aliases `q0`…`q3`). Q0=+Bh (+h in tilt), Q1=+Bv (+Y), Q2=−Bh, Q3=−Bv. If the field is off this starts revolve (default Z, or the current tilt/X/Z program if already selected) and sine-swells into that corner. If already revolving, phase travels the circle and holds. `corner run` resumes stepping. You can change program / `rot_dir` / `amp` / `gamma` while the field is on; live amplitude fades through 0 before the new setpoint. `axis y|x|z|tilt` selects the program without starting.

Host math checks (no hardware):

```text
python3 monitor/test_field_waveforms.py
bash monitor/run_field_tests.sh
```

**Ramps** (`rotate off`, `revolve off`, START, amp, CW↔CCW, rotate↔revolve): live amplitude steps toward the target (~1.3 s 0↔255). Revolve additionally freezes phase and sine-shapes that envelope so |A| swells/shrinks at a cardinal.

**Immediate cuts:** `off all`, web **ALL OFF**, DC writes to M1–M4, `contour`, `program` (including `program stop`).

`status` reports commanded `program` / `axis` / `rot_dir` / `gamma`, plus `amp` and `amp_live`. `json` also reports `program_live`, `axis_live`, `rot_dir_live`, `gamma_live`, `tilt_scale`, `rev_gain` (|B| per amp for the commanded plane) (what the sine sample is using during a fade), and `mlx`: `ok`, `n` (chips present), `sat`, top-level `bx/by/bz/babs` (reconstructed centre, mT), `mlx.center` (same), and `mlx.coils[4]` (`ok`, `addr`, `sat`, `k`, per-chip lab `bx/by/bz/babs`). The JSON `rotate` flag is 1 whenever the sine sample is running (rotate or revolve). PWM-proxy units are not millitesla. `fb`: `en`, `ok`, `err` (%), `updates`, `fault` (bit per refused coil), `gain` (mT per duty), `k[4]` (correction magnitude), `ph[4]` (correction phase, deg).

### MLX behind coils

With `fb off` (default), coil PWM is the open-loop geometric 30° / `rev_hv` law and the chips are display-only.

```text
mlx scan                     # bind found addrs → M1..M4
mlx bind 0x0C 0x0D 0x0E 0x0F # optional explicit map (0 = auto)
mlx map M1 x y z             # raw chip axes → x up / y left / z axial (omit M# for all)
mlx zsign M3 -               # chip +z against M3's +duty field
mlx offset                   # coils off → B0 per chip
mlx k all 1                  # reconstruction scale
revolve z 0.05 180
teleplot on                  # overlay Bx_c / M1_bx_m …
```

If a reading saturates a chip, lower `amp`. `rev_hv` still trims the commanded ellipse.

### MLX feedback (`fb`)

A toggle (CLI `fb on|off`, web **MLX feedback** switch) that corrects the PWM so the centre field measured by the four chips follows the commanded circle. It works for rotate Y and every revolve plane (any γ), CW or CCW.

How it works:

- Every program drives each coil with one sine in the synthesis phase θ: \(u_i=lpha_i\cos	heta+eta_i\sin	heta\), phasor \(U_i=lpha_i-jeta_i\).
- Chip i's axial reading \(m_i=\mathrm{zsign}_i B_{z,i}/k_i\) is coil i's contribution at the centre (the same value the reconstruction uses). Over about one revolution of steady field it is least-squares fitted as \(m_ipprox p\cos	heta+q\sin	heta+r\), so \(M_i=p-jq\) (r absorbs leftover offset).
- Per-coil gain \(G_i=M_i/(K_iU_i)\) (mT per duty, including that coil's lag). Best common gain \(\hat G=\sum M_iU_i^*/\sum|U_i|^2\).
- Correction \(K_i\leftarrow K_i+\mu(\hat G/G_i-K_i)\). Coil i is then driven at \(|K_i|\,u_i(	heta+rg K_i)\): rescaled and phase-advanced so every coil gives the same mT per duty, so the measured field becomes \(\hat G	imes\) the commanded shape.
- \(|\hat G|\) is capped by the weakest coil's headroom, so a corrected coil never needs more than ±255 duty. At `amp 255` the corrected circle can be a bit smaller than the uncorrected one.
- Delays common to all four chips (conversion, polling) only shift \(rg\hat G\) and cancel. The fit uses θ, not time, so it still works when 50 Hz sampling is coarse; windows with poor phase coverage (e.g. exactly 25 Hz) are rejected.

When it learns: only while the field is steady (no amp ramp, START/STOP envelope, corner park or γ/program/direction change). Each update needs about 1.1 revolutions of chip data (at least 40 sets, so ~1 s at ≥1.4 Hz, ~110 s at 0.01 Hz). Changing `freq` restarts from K = 1 because coil lag depends on frequency. Changing direction conjugates K. `amp`, γ and program changes keep K.

Safety: a coil whose correction would need more than ±75° of phase, or |K| outside 0.25–2.5, is refused (`fault` bit, `REFUSED` in `fb` / `status`) and keeps its previous drive. A 180° error almost always means a wrong `zsign`. `mlx off` freezes the correction.

```text
mlx scan                     # all 4 chips needed
mlx offset                   # zero Earth/bias first
mlx zsign M3 -               # per coil: +duty must read +axial
revolve x 1 150
teleplot on                  # Byz_m vs Byz_e, fb_err
fb                           # open-loop error and per-coil gain/phase
fb on                        # correct; err should fall over a few revolutions
fb rate 0.3                  # slower, steadier learning (default 0.5)
fb reset                     # forget the correction (K = 1)
fb off                       # back to open-loop PWM
```

The loop equalises the four coils as the chips see them. Field from the other coils reaching a chip (crosstalk) is part of what it corrects towards, so the result is only as true as the reconstruction (chip placement, `k`, `zsign`).

## Project layout

```text
├── platformio.ini          # ESP32-WROOM-32 Arduino env
├── include/
│   ├── config.h            # Pins, SoftAP, NUM_COILS, LUT size
│   ├── www_page.h          # Embedded lab web UI
│   ├── field_tilt.h        # 30° four-coil inverse + reconstruct (host-testable)
│   ├── field_engine.h      # Phase, ramp, rotate/revolve sample
│   ├── mlx90393.h          # Magnetometer driver (I2C1, up to 4 chips)
│   └── …
├── src/
│   ├── main.cpp            # Timer, tasks, WiFi, HTTP, UART
│   ├── field_engine.c      # LUT, 1 kHz sample, ramps
│   ├── field_tilt.c        # 30° per-coil sine law + reconstruct
│   ├── pca9685.c           # I2C0 PWM expander
│   ├── coil_output.c       # EN GPIO + duty shadows
│   ├── mlx90393.c          # I2C1 MLX90393 driver
│   ├── mlx_app.c           # Poll + centre reconstruct
│   ├── field_fb.c          # MLX feedback estimator (host-testable)
│   ├── fb_app.c            # Feedback toggle / glue
│   ├── cli.c               # Command parser + json/Teleplot
│   └── pad.cpp             # Bluepad32 DualSense → CLI queue
├── monitor/
│   ├── dualsense_field.py  # Optional UART0 pad mapper
│   ├── test_field_waveforms.py
│   ├── test_field_tilt.c
│   ├── test_field_fb.c     # Feedback convergence on a simulated rig
│   └── run_field_tests.sh
└── CoilController.code-workspace
```

## License / research use

Research firmware for soft-magnetic composite coil experiments (HKUST). Confirm EN polarity and 3.3 V I2C on your hardware before continuous high-current operation.
