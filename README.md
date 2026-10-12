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
                      PCA9685      MLX90393           │
                      PWM ch0–7    (centre B)         │
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
- Lab frame: **Y is vertical** (out of plane); **X and Z are horizontal**. Coils are **tilted ~45°** so each contributes both horizontal and vertical field at the centre
- **PCA9685 PWM carrier ~1 kHz** (chip max ~1.5 kHz), duty **0–255** mapped to 12-bit
- **DC mode:** constant magnitude + direction per coil (open- or closed-loop IS)
- **Rotate Y** (field in the **XZ** plane, all four coils)
  - M1 = M3 ∝ cos ωt, M2 = M4 ∝ sin ωt
  - Each pair shares the **same current direction**; the two pairs are 90° apart
  - CW: M2∥M4 = +sin; CCW: M2∥M4 = −sin
- **Revolve Z** (field in the **XY** plane)
  - Only M2 = I₁, M4 = I₂. M1 and M3 off
  - \(A_h=\mathrm{amp}\cdot h/100\), \(A_v=\mathrm{amp}\cdot v/100\) (`rev_hv`; 100/100 is a circle). \(B_h=A_h\cos\theta\), \(B_v=A_v\sin\theta\), \(I_1=(\sqrt{2}/2)(A_v\sin\theta-A_h\cos\theta)\), \(I_2=-(\sqrt{2}/2)(A_h\cos\theta+A_v\sin\theta)\). Equal radii recover \(I_1=\mathrm{amp}\cdot\sin(\theta-\pi/4)\). If real H is 4× V, use `rev_hv 25 100` (do not boost V 4× — PWM saturates)
- **Revolve X** (field in the **YZ** plane)
  - Only M1 = I₁, M3 = I₂. M2 and M4 off
  - Same constant-|B| circle with \(B_z\) in place of \(B_x\)
- **Revolve A scale:** `rev_hv <h%> <v%>` sets \(A_h=\mathrm{amp}\cdot h/100\), \(A_v=\mathrm{amp}\cdot v/100\) (1–200%; 100/100 = circle). Rotate Y ignores this
- **MLX90393** at the sample centre (I2C2) streams measured \(B\) in mT for display / Teleplot / JSON. It does **not** correct coil PWM
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

### MLX90393 centre magnetometer

Put the probe at the geometric centre. **Do not share I2C1** with the PCA9685 — mag traffic would stall coil PWM.

**I2C (STM32 → MLX90393)**

| Signal | STM32 | MLX90393 / breakout |
|--------|-------|---------------------|
| SCL | **PB10** (I2C2) | SCL |
| SDA | **PB11** (I2C2) | SDA |
| VIN / VDD | **3.3 V** | 2.2–3.6 V (bare chip: also tie VDD_IO to 3.3 V) |
| GND | GND | GND |
| CS / SENB | — | **3.3 V** (I2C mode). Leave MISO floating. |
| A0, A1 | — | GND for the base address (typical breakout default) |
| INT | optional PB12 | data-ready; firmware polls, pin not required |

Add **4.7 kΩ** pull-ups to 3.3 V on SCL/SDA if the breakout does not already have them. 100 nF on VDD (and VDD_IO on a bare QFN).

Default 7-bit address: **0x18** on Adafruit boards after May 2023, **0x0C** on older units / ABA-011 with A0=A1=0. `mlx scan` probes `0x0C`–`0x1B`.

Range is gain-programmable **±5…±50 mT**. Firmware boots at analog 1× + 19-bit encoding (~±39 mT XY). If `mlx` reports SAT, lower `amp`.

Chip X/Y/Z vs lab frame (Y vertical, X/Z horizontal) is remapped with `mlx map x y z` (prefix `-` to flip an axis). If the breakout lies in the coil XY plane, chip Z is often lab Y: `mlx map x z -y` or similar — confirm with `corner Q0` (+Bh, should be +lab X for revolve Z).

### Onboard ST7735 (160×80)

| Signal | Pin |
|--------|-----|
| SPI4 SCK | PE12 |
| SPI4 MOSI | PE14 |
| CS | PE11 |
| DC | PE13 |
| BL (active low) | PE10 |

Shows mode/contour or `rotY fHz …mT` / `revX` / `revZ` while the AC field is on, four coil rows, plus a measured **B** row (`B+12.1-0.3+0.1`, `!` = saturation) at ~10 Hz.

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
   - **Field** — Rotate Y / Revolve X / Revolve Z, START/STOP, freq/amp, CW/CCW, soft limit, MLX cal/apply. Canvas is a **math preview** of the commanded waveform (bipolar signed duty, or unipolar RPWM/LPWM). Last `json` sample shows live `signed` / `pr` / `pl` and measured `mlx` mT. For a live scope, use Teleplot on USART1 (below).
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
rotate on|off
rotate on 2.5 160
revolve z [Hz] [0..255]
revolve x [Hz] [0..255]
revolve off
rev_hv 100 80
freq 5
amp 180
rot_dir cw|ccw
corner Q0|Q1|Q2|Q3|run
teleplot on|off
mlx
mlx scan
mlx map x y z
mlx offset
```

### Waveform observe (Teleplot)

Live commanded duties (bipolar `signed` and unipolar `RPWM`/`LPWM`) stream on **USART1 only** at ~50 Hz when enabled. Off by default so UART does not starve PCA9685 flushes.

```text
pio device monitor
teleplot on
```

Open [Teleplot](https://github.com/nesnes/teleplot) (or the VS Code Teleplot extension) on that serial port.

Coil PWM series: `M1_signed`…`M4_signed`, `M*_rpwm`, `M*_lpwm`, plus `amp_live`.

Lab **B** (PWM-duty proxy, not Tesla — signed duty is treated as coil current). Each coil is tilted 45°, so the pair map is \(B_h=-(√2/2)(B_1+B_2)\), \(B_v=(√2/2)(B_1-B_2)\):

- Time: `Bx` (XY horizontal, M2/M4), `By` (vertical, both pairs), `Bz` (YZ horizontal, M1/M3), `Babs` \(=\sqrt{B_x^2+B_y^2+B_z^2}\)
- XY plots: `Bxy` (Bx vs By), `Bxz` (Bz vs Bx), `Byz` (Bz vs By)

Rotate Y: `By≈0`, `Bx`/`Bz` in quadrature, `Babs` nearly flat at ~amp√2 (circle on `Bxz`). Revolve Z: `Babs` stays at amp (circle on `Bxy`). Revolve X: circle on `Byz`. Hide the plane plots you do not need.

**Measured MLX** (mT, Earth/bias subtracted after `mlx offset`): `Bx_m`, `By_m`, `Bz_m`, `Babs_m`, plus `Bxy_m` / `Bxz_m` / `Byz_m` xy-plots. Overlay these on the PWM-proxy traces. A wide measured ellipse with a PWM circle is unequal H/V gain — coil PWM stays geometric; use `rev_hv` if you want to change the commanded ellipse. The MLX never feeds back into coil outputs.

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

**Revolve Z** (XY plane, M2=I₁, M4=I₂). Equal H/V (circle in PWM-proxy B):

```text
rev_hv 100 100
revolve z 2.5 160
```

If real H is about 4× V, cut the horizontal command 4× (fits PWM; boosting V 4× saturates):

```text
rev_hv 25 100
revolve z 0.05 180
```

**Revolve X** (YZ plane, M1=I₁, M3=I₂, same \(A_h\)/\(A_v\)):

```text
revolve x 2.5 160
```

Park at a cardinal with `corner Q0` … `Q3` (aliases `q0`…`q3`). Q0=+Bh, Q1=+Bv, Q2=−Bh, Q3=−Bv. If the field is off this starts revolve (default Z) and sine-swells into that corner. If already revolving, phase travels the circle and holds. `corner run` resumes stepping. You can change program / `rot_dir` / `amp` while the field is on; live amplitude fades through 0 before the new setpoint. `axis y|x|z` selects the program without starting.

**Ramps** (`rotate off`, `revolve off`, START, amp, CW↔CCW, rotate↔revolve): live amplitude steps toward the target (~1.3 s 0↔255). Revolve additionally freezes phase and sine-shapes that envelope so both active coils swell into \(+B_h\) (both \(\approx-A_h/\sqrt{2}\)) before stepping, and shrink in place on stop.

**Immediate cuts:** `off all`, web **ALL OFF**, DC writes to M1–M4, `contour`, `program` (including `program stop`).

`status` reports commanded `program` / `axis` / `rot_dir`, plus `amp` and `amp_live`. `json` also reports `program_live`, `axis_live`, `rot_dir_live` (what the sine ISR is using during a fade), and an `mlx` object (`ok`, `bx/by/bz/babs` in mT). The JSON `rotate` flag is 1 whenever the sine ISR is running (rotate or revolve).

### MLX centre probe (display only)

The MLX90393 streams measured \(B\) in mT. Coil PWM is always the geometric 45° / `rev_hv` law — measured \(B\) is never used to correct outputs.

```text
mlx scan          # must see addr 0x0C or 0x18
mlx offset        # coils off → B0 (Earth + bias), subtracted from display
corner Q0         # check +Bh on Bx_m; mlx map if axes are rotated
revolve z 0.05 180
teleplot on       # overlay Bx_m / Bxy_m on the PWM-proxy traces
```

If a reading saturates the chip, lower `amp`. Use `rev_hv` to change the commanded H/V ellipse; the probe does not close that loop.

## Project layout

```text
├── platformio.ini          # STM32H743 env
├── include/
│   ├── st7735.h            # Onboard TFT driver API
│   ├── display_ui.h        # Live coil status layout
│   └── mlx90393.h          # Centre magnetometer (I2C2)
├── src/
│   ├── main.c              # Coil PWM, sine ISR, ADC, dual-UART CLI, MLX cal
│   ├── mlx90393.c          # I2C2 MLX90393 driver
│   ├── st7735.c            # SPI4 + ST7735
│   └── display_ui.c        # Status screen (~10 Hz)
├── esp32_bridge/
│   ├── platformio.ini      # ESP32-S3 Arduino env
│   ├── include/config.h    # SoftAP + UART pins
│   ├── include/www_page.h  # Embedded lab web UI
│   └── src/main.cpp        # SoftAP, HTTP API, UART bridge
└── CoilController.code-workspace
```

## License / research use

Research firmware for soft-magnetic composite coil experiments (HKUST). Adapt pin maps and current-sense calibration for your hardware before continuous high-current operation.
