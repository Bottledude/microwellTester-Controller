# Soft Magnetic Composite Controller

PlatformIO firmware for driving **four** soft-magnetic coils with a **WeAct Mini STM32H743VIT6**, a **PCA9685** PWM expander, and **BTS7960 / IBT-2** half-bridge drivers. An optional **ESP32-S3 N16R8** provides a WiFi SoftAP and lab-oriented browser UI while the STM32 Serial Monitor CLI remains available in parallel.

**Repo:** https://github.com/Bottledude/microwellTester-Controller

This firmware is **four coils only** (M1–M4). PWM goes out the PCA9685; **EN and IS stay on the STM32**. Flash **both** the STM32 and the ESP32 after pulling this tree.

## Architecture

```text
    Browser  ──WiFi SoftAP──►  ESP32-S3          Serial Monitor (USB-UART)
                              │                      │
                         UART 115200            USART1 115200
                         GPIO17/18 ↔ PD8/PD9     PA9/PA10
                              │                      │
                              └────────┬─────────────┘
                                       ▼
                              STM32H743VIT6
                         ┌─────────────┼──────────────┐
                   I2C1 PB8/PB9   I2C2 PB10/PB11   EN GPIO + IS ADC
                         │             │              │
                      PCA9685      4× MLX90393        │
                      PWM ch0–7    (behind M1–M4)     │
                         │                            │
                         └────────────┬───────────────┘
                                      ▼
                           BTS7960 ×4 → coils (~2 A each)
```

| Role | Board | Firmware |
|------|--------|----------|
| Coil driver / CLI | WeAct Mini STM32H743VIT6 | `src/main.c` + root `platformio.ini` |
| WiFi web bridge | ESP32-S3 N16R8 | `esp32_bridge/` |

## Features

- **4 coils** at **90°** around the sample, each on its own BTS7960 (RPWM / LPWM / EN / IS)
- Lab frame: **Y is vertical** (out of plane); **X and Z are horizontal**. Coils are **tilted 30° from horizontal** (`c_h=√3/2`, `c_v=1/2`) so each contributes both horizontal and vertical field at the centre
- **PCA9685 PWM carrier ~1 kHz** (chip max ~1.5 kHz), duty **0–255** mapped to 12-bit
- **DC mode:** constant magnitude + direction per coil (open- or closed-loop IS)
- **Rotate Y** (field in the **XZ** plane, all four coils)
  - M1 = M3 ∝ cos ωt, M2 = M4 ∝ sin ωt
  - Each pair shares the **same current direction**; the two pairs are 90° apart
  - CW: M2∥M4 = +sin; CCW: M2∥M4 = −sin
- **Revolve X / Z / tilt** share one **four-coil 30° inverse** (`field_tilt.c`). The primary pair carries horizontal field; the secondary pair supplies extra \(B_y\) so \(|B_v|\) at ±Y matches \(|B_h|\) at full amp — no geometric need to cut horizontal `rev_hv`
  - Revolve X: \(\gamma=0\) (YZ plane). Revolve Z: \(\gamma=90\) (XY plane, Q0 is **−X**). Tilt: live \(\gamma\) slider
  - \(A_h=\mathrm{amp}\cdot h/100\), \(A_v=\mathrm{amp}\cdot v/100\) (`rev_hv`; 100/100 is a PWM-proxy circle). Unequal radii are an ellipse
  - Forward: \(B_x=-c_h(u_2+u_4)\), \(B_y=c_v(u_1+u_2-u_3-u_4)\), \(B_z=-c_h(u_1+u_3)\)
  - Changing `gamma` while running ramps live amp through 0, then commits \(\sin\gamma/\cos\gamma\) together
- **Revolve A scale:** `rev_hv` remains an empirical H/V trim (1–200%). Rotate Y ignores this. Shared scale if any coil would exceed 255
- **4× MLX90393** behind M1–M4 (I2C2) reconstruct centre \(B\) via coil-axis projection + the same 30° forward map. Display / Teleplot / JSON only — never drives PWM
- Shared **frequency** (0.01–50 Hz, default 1 Hz) and **amplitude** (0–255, **default 0** — set `amp` before START). Sine LUT is **360** samples (1°).
- ~1 kHz sine sample ISR; I2C pushes the latest duty to the PCA from the main loop
- **Amp ramp** (~1.3 s for 0↔255) on start, stop, amp changes, CW↔CCW, and rotate↔revolve. Revolve start/stop **freezes phase** and maps that ramp through a quarter-sine envelope so |A| swells/shrinks smoothly at a cardinal.
- Soft current limits (`limit`); boot-on all coils at ~2 A; over-limit derates `amp_live`
- Dual CLI ports: human Serial Monitor + machine ESP32 bridge (`ping` / `json`)
- Lab web UI tabs: **Field**, **DC coils**, **Logs** (sticky **ALL OFF**)
- Onboard **0.96" ST7735** status UI

## Hardware

### PCA9685 PWM + STM32 EN / IS

RPWM/LPWM go to the **PCA9685**, not the STM32 timers. EN and current-sense stay on the H743.

**I2C (STM32 → PCA9685)**

| Signal | STM32 | PCA9685 |
|--------|-------|---------|
| SCL | **PB8** (I2C1) | SCL |
| SDA | **PB9** (I2C1) | SDA |
| GND | GND | GND |
| — | — | **OE → GND** (outputs enabled) |
| — | — | VCC = 5 V (3.3 V I2C is OK on Adafruit-style boards) |

Default 7-bit address **0x40** (A0–A5 = 0). Override with `-DPCA9685_I2C_ADDR=0x41` if you strapped A0.

| Coil | Place | PCA RPWM | PCA LPWM | EN (STM32) | IS (ADC) |
|------|-------|----------|----------|------------|----------|
| M1 | YZ pair (revolve x / rotate-Y cos) | ch0 | ch1 | **PD10** | PA0 |
| M2 | XY pair (revolve z / rotate-Y sin) | ch2 | ch3 | **PE4** | PA1 |
| M3 | YZ pair (revolve x / rotate-Y cos) | ch4 | ch5 | **PC4** | PA2 |
| M4 | XY pair (revolve z / rotate-Y sin) | ch6 | ch7 | **PC5** | PA3 |

IS stays on **PA0–PA3**. The PCA carrier is ~1 kHz (not 40 kHz). Rotate / revolve still work; the sine is updated at 1 kHz in RAM and pushed over I2C from the main loop.

IBT-2 / BTS7960: tie **R_EN + L_EN** to the STM32 EN pin, logic **VCC = 5 V**, **VS** = coil supply. IS → STM32 ADC (see `IS_RIS_OHM` / `IS_K_RATIO` in `src/main.c`).

### MLX90393 behind each coil

Four chips share **I2C2**. Mount one behind each coil (not at the sample centre). Unique 7-bit addresses via A0/A1/CS straps. **Do not share I2C1** with the PCA9685.

**I2C (STM32 → MLX90393 ×4)**

| Signal | STM32 | MLX90393 / breakout |
|--------|-------|---------------------|
| SCL | **PB10** (I2C2) | SCL (bus) |
| SDA | **PB11** (I2C2) | SDA (bus) |
| VIN / VDD | **3.3 V** | 2.2–3.6 V |
| GND | GND | GND |
| CS / SENB | — | **3.3 V** (I2C mode) |
| A0, A1 | — | strap uniquely per coil |
| INT | unused | firmware polls |

Add **4.7 kΩ** pull-ups to 3.3 V on SCL/SDA if the boards do not already have them.

`mlx scan` probes `0x0C`–`0x1B` and binds sorted addresses to M1–M4. Override with `mlx bind <a1> <a2> <a3> <a4>` (`0` = auto for that slot). Chip-frame → lab: `mlx map [M#] x y z` (omit `M#` to apply all). Reconstruction scale: `mlx k <M#|all> <s>` (default 1). `mlx offset` takes Earth/bias \(B_0\) with coils off.

Reconstruction: \(u_i=(B_i\cdot\hat{a}_i)/k_i\) (missing chips → 0), then the 30° forward map. Coil axes from that map:

| Coil | \(\hat{a}\) (lab) |
|------|-------------------|
| M1 | \((0,\ c_v,\ -c_h)\) |
| M2 | \((-c_h,\ c_v,\ 0)\) |
| M3 | \((0,\ -c_v,\ -c_h)\) |
| M4 | \((-c_h,\ -c_v,\ 0)\) |

Range boots at analog 1× + 19-bit (~±39 mT XY). If a chip reports SAT, lower `amp`. Hall data never feeds PWM.

### Onboard ST7735 (160×80)

| Signal | Pin |
|--------|-----|
| SPI4 SCK | PE12 |
| SPI4 MOSI | PE14 |
| CS | PE11 |
| DC | PE13 |
| BL (active low) | PE10 |

Shows mode/contour or `rotY fHz …mT` / `revX` / `revZ` / `tilt` while the AC field is on, four coil rows, plus a reconstructed centre **B** row (`B+12.1-0.3+0.1 4/4`, `n/4` = chips present, `!` = saturation) at ~10 Hz.

### ESP32 ↔ STM32 bridge (3.3 V, common GND)

| ESP32-S3 | STM32 H743 |
|----------|------------|
| GPIO17 (TX) | PD9 (USART3_RX) |
| GPIO18 (RX) | PD8 (USART3_TX) |
| GND | GND |

Cross TX/RX (do not wire TX–TX). Serial Monitor uses **USART1 PA9/PA10 @ 115200**.

## Build & flash

Requires [PlatformIO](https://platformio.org/) (prefer `~/.platformio/penv/bin/pio` over the Ubuntu apt package).

### STM32H743

```bash
cd /path/to/microwellTester-Controller
pio run -t upload
pio device monitor   # 115200
```

Board env: `weact_mini_h743vitx` (STM32Cube). Useful `build_flags` in `platformio.ini`: `PWM_TARGET_HZ` (PCA carrier, clamp 24–1526 Hz), `HSE_VALUE`. Optional `-DROTATE_RAMP_STEP=N` (live-amp LSB per 200 Hz tick; default `1` ≈ 1.3 s for 0↔255). `NUM_COILS` must stay **4**.

### ESP32-S3 bridge

```bash
pio run -d esp32_bridge -t upload
```

WiFi SSID/password and UART pins: `esp32_bridge/include/config.h` (`NUM_COILS` is also 4 there).

## Web UI

1. Join SoftAP **`CoilControl`** / **`coilcontrol`**
2. Open **http://192.168.4.1/**
3. Tabs:
   - **Field** — Rotate Y / Revolve X / Revolve Z / Revolve tilt, START/STOP, freq/amp, γ, CW/CCW, soft limit, MLX scan/offset. Canvas is a **math preview** of the commanded waveform (bipolar signed duty, or unipolar RPWM/LPWM). Last `json` sample shows live `signed` / `pr` / `pl` and reconstructed `mlx.center` mT. For a live scope, use Teleplot on STM32 USART1 (below) — not the ESP32 UART.
   - **DC coils** — per-coil constant drive, contour, program P1/P2. All four lock while the AC field is on (STOP first).
   - **Logs** — command traffic + raw CLI
4. Field **STOP** sends `rotate off` (amp fades; also stops revolve). Header **ALL OFF** sends `rotate off` then `off all` — **`off all` cuts immediately**.
5. Link pill is **linked** only after a parsed `json` coil snapshot. Ping-only shows **UART up · status stale**.

## Serial CLI (STM32)

Both USART1 and USART3 accept the same commands (`help` for the full list). Motors are **M1–M4** only. Incomplete lines print usage; unknown motor tokens print an error. Examples:

```text
help
ping
json
status
sense
mode open|closed
on M1 M2
off all
set M1 180
dir M1 +
drive M2 200 -
limit all 255
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

### DualSense pad (laptop → USART1)

The DualSense stays on the **PC** (Bluetooth or USB). A host script maps buttons/sticks to the same CLI and writes them to the Serial Monitor UART. The H743 is not a USB-HID host — do not plug the pad into the STM32.

```text
pip install -r monitor/requirements-gamepad.txt
python monitor/dualsense_field.py --list          # serial ports
python monitor/dualsense_field.py --port COM9     # USART1 / ST-Link VCP; not DualSense Bluetooth COM ports
python monitor/dualsense_field.py --dry-run       # pad only, no UART
```

This process **owns USART1**. Close `pio device monitor` and Teleplot on that port first (they cannot share the COM port). USART3 / the ESP32 web UI can stay connected.

| Pad | CLI |
|-----|-----|
| D-pad Up / Left / Right / Down | `axis y` / `x` / `z` / `tilt` |
| Options | START (`rotate on` or `revolve <axis>`) |
| Create | STOP (`rotate off`) |
| PS | `off all` (immediate) |
| L1 / R1 | `rot_dir ccw` / `cw` |
| □ / ✕ / ○ / △ | `corner Q0` / `Q1` / `Q2` / `Q3` |
| Left stick X | `gamma 0..90` (5° steps; rest holds) |
| Right stick X | `amp 0..255` (rest holds) |
| Right stick Y | slew `freq` |
| Touchpad click | `status` |

If face buttons are swapped, pass `--layout sony`. `--debug` prints raw button numbers. Changing `gamma` / program while the field is on still ramps amp through 0.

### Waveform observe (Teleplot)

Install the [Teleplot](https://marketplace.visualstudio.com/items?itemName=alexnesnes.teleplot) extension (`alexnesnes.teleplot`). Cursor often does not list it in the marketplace UI — download the `.vsix` and install it:

```text
# latest package (save as teleplot.vsix; the API may gzip the download)
https://marketplace.visualstudio.com/_apis/public/gallery/publishers/alexnesnes/vsextensions/teleplot/latest/vspackage

cursor --install-extension path\to\teleplot.vsix
```

Or Command Palette → **Extensions: Install from VSIX…**. Then **Developer: Reload Window**, **Teleplot**, and connect to the STM32 USART1 port at **115200**.

Live commanded duties (bipolar `signed` and unipolar `RPWM`/`LPWM`) stream on **USART1 only** at ~50 Hz when enabled. Off by default so UART does not starve PCA9685 flushes. Do not open Teleplot on the ESP32 SoftAP UART.

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

Rotate Y: `By≈0`, `Bx`/`Bz` in quadrature, `Babs` nearly flat at ~amp√3 (circle on `Bxz`). Revolve X/Z/tilt: `Babs` stays at amp; tilt adds `Bn≈0` plus `gamma`, `Bh`, `Bv`, `Bn`, `tilt_scale`.

**Reconstructed centre MLX** (mT): `Bx_c` / `By_c` / `Bz_c` / `Babs_c` (aliases `Bx_m`…). XY plots `Bxy_m` / `Bxz_m` / `Byz_m`. Per-coil lab vectors: `M1_bx_m`…. The Hall path never feeds back into coil outputs.

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

`gamma 0` is the YZ plane (axis +X). `gamma 90` is the XY plane (axis +Z). Out-of-range values (`gamma 91`, `gamma -1`) are rejected and leave state unchanged. Unequal `rev_hv` in this mode is an ellipse. If a coil would exceed ±255, all four commands share one scale (`tilt_scale` in `status` / `json` / Teleplot).

Park at a cardinal with `corner Q0` … `Q3` (aliases `q0`…`q3`). Q0=+Bh (+h in tilt), Q1=+Bv (+Y), Q2=−Bh, Q3=−Bv. If the field is off this starts revolve (default Z, or the current tilt/X/Z program if already selected) and sine-swells into that corner. If already revolving, phase travels the circle and holds. `corner run` resumes stepping. You can change program / `rot_dir` / `amp` / `gamma` while the field is on; live amplitude fades through 0 before the new setpoint. `axis y|x|z|tilt` selects the program without starting.

Host math checks (no hardware):

```text
python3 monitor/test_field_waveforms.py
bash monitor/run_field_tests.sh
```

**Ramps** (`rotate off`, `revolve off`, START, amp, CW↔CCW, rotate↔revolve): live amplitude steps toward the target (~1.3 s 0↔255). Revolve additionally freezes phase and sine-shapes that envelope so |A| swells/shrinks at a cardinal.

**Immediate cuts:** `off all`, web **ALL OFF**, DC writes to M1–M4, `contour`, `program` (including `program stop`).

`status` reports commanded `program` / `axis` / `rot_dir` / `gamma`, plus `amp` and `amp_live`. `json` also reports `program_live`, `axis_live`, `rot_dir_live`, `gamma_live`, `tilt_scale` (what the sine ISR is using during a fade), and `mlx`: `ok`, `n` (chips present), `sat`, top-level `bx/by/bz/babs` (reconstructed centre, mT), `mlx.center` (same), and `mlx.coils[4]` (`ok`, `addr`, `sat`, `k`, per-chip lab `bx/by/bz/babs`). The JSON `rotate` flag is 1 whenever the sine ISR is running (rotate or revolve). PWM-proxy units are not millitesla; the MLX never feeds the coil equations.

### MLX behind coils (display only)

Coil PWM is always the geometric 30° / `rev_hv` law — measured \(B\) is never used to correct outputs.

```text
mlx scan                     # bind found addrs → M1..M4
mlx bind 0x0C 0x0D 0x0E 0x0F # optional explicit map (0 = auto)
mlx map M1 x y z             # chip-frame → lab (omit M# for all)
mlx offset                   # coils off → B0 per chip
mlx k all 1                  # reconstruction scale
revolve z 0.05 180
teleplot on                  # overlay Bx_c / M1_bx_m …
```

If a reading saturates a chip, lower `amp`. `rev_hv` still trims the commanded ellipse; sensors do not close that loop.

## Project layout

```text
├── platformio.ini          # STM32H743 env
├── .vscode/extensions.json # PlatformIO + Teleplot (alexnesnes.teleplot)
├── include/
│   ├── st7735.h            # Onboard TFT driver API
│   ├── display_ui.h        # Live coil status + reconstructed B
│   ├── mlx90393.h          # Magnetometer driver (I2C2, up to 4 chips)
│   └── field_tilt.h        # 30° four-coil inverse + reconstruct (host-testable)
├── src/
│   ├── main.c              # Coil PWM, sine ISR, ADC, dual-UART CLI, 4× MLX
│   ├── field_tilt.c        # 30° equal-V synthesis + reconstruct
│   ├── mlx90393.c          # I2C2 MLX90393 driver
│   ├── st7735.c            # SPI4 + ST7735
│   └── display_ui.c        # Status screen (~10 Hz)
├── monitor/
│   ├── dualsense_field.py
│   ├── requirements-gamepad.txt
│   ├── test_field_waveforms.py
│   ├── test_field_tilt.c
│   └── run_field_tests.sh
├── esp32_bridge/
│   ├── platformio.ini      # ESP32-S3 Arduino env
│   ├── include/config.h    # SoftAP + UART pins
│   ├── include/www_page.h  # Embedded lab web UI
│   └── src/main.cpp        # SoftAP, HTTP API, UART bridge
└── CoilController.code-workspace
```

## License / research use

Research firmware for soft-magnetic composite coil experiments (HKUST). Adapt pin maps and current-sense calibration for your hardware before continuous high-current operation.
