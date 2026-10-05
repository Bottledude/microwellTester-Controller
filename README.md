# Soft Magnetic Composite Controller

PlatformIO firmware for driving soft-magnetic coils with a **WeAct Mini STM32H743VIT6** and up to six **BTS7960 / IBT-2** half-bridge drivers. An optional **ESP32-S3 N16R8** provides a WiFi SoftAP and lab-oriented browser UI while the STM32 Serial Monitor CLI remains available in parallel.

**Repo:** https://github.com/Bottledude/softMagneticComposite-Controller

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
                                       │
                          PWM + EN + IS (ADC)
                                       ▼
                         BTS7960 ×1…6 → coils (~2 A each)
```

| Role | Board | Firmware |
|------|--------|----------|
| Coil driver / CLI | WeAct Mini STM32H743VIT6 | `src/main.c` + root `platformio.ini` |
| WiFi web bridge | ESP32-S3 N16R8 | `esp32_bridge/` |

## Features

- Up to **6 coils**, each on its own BTS7960 (RPWM / LPWM / EN / IS)
- **~40 kHz PWM carrier**, duty **0–255**
- **DC mode:** constant magnitude + direction per coil (open- or closed-loop IS)
- **Rotating field mode:** open-loop quadrature sine on **M1–M4**
  - Phase A = M1 ∥ M3 @ 0°
  - Phase B = M2 ∥ M4 @ +90° (CW) or −90° (CCW)
  - Shared **frequency** (0.1–50 Hz, default 1 Hz) and **amplitude** (0–255, **default 0** — set `amp` before `rotate on`)
  - ~10 kHz sine update ISR for high waveform resolution
- Soft current limits (`limit`); boot-on all coils at ~2 A; while rotating, over-limit **derates shared live amplitude** (`amp_live` in `status` / `json`)
- Dual CLI ports: human Serial Monitor + machine ESP32 bridge (`ping` / `json`)
- Lab web UI tabs: **Field**, **DC coils**, **Logs** (sticky **ALL OFF**)
- Onboard **0.96" ST7735** status UI

## Hardware

### Coil / BTS7960 pin map (STM32)

| Coil | RPWM | LPWM | EN | IS (ADC) | Timer |
|------|------|------|-----|----------|-------|
| M1 | PD14 | PD15 | PD10 | PA0 | TIM4 |
| M2 | PE5 | PE6 | PE4 | PA1 | TIM15 |
| M3 | PC6 | PC7 | PC5 | PA2 | TIM8 |
| M4 | PC8 | PC9 | PC4 | PA3 | TIM8 |
| M5 | PA6 | PA7 | PB10 | PA4 | TIM3 |
| M6 | PB0 | PB1 | PB11 | PA5 | TIM3 |

M1/M2 were remapped off PE10–PE14 so the WeAct onboard TFT can use SPI4.

IBT-2 / BTS7960: tie **R_EN + L_EN** to EN, logic **VCC = 5 V**, **VS** = coil supply. IS → STM32 ADC (see `IS_RIS_OHM` / `IS_K_RATIO` in `src/main.c`).

### Onboard ST7735 (160×80)

| Signal | Pin |
|--------|-----|
| SPI4 SCK | PE12 |
| SPI4 MOSI | PE14 |
| CS | PE11 |
| DC | PE13 |
| BL (active low) | PE10 |

Shows mode/contour or `ROT fHz Aamp` while rotating (**live** amplitude after soft-limit derate), plus one status row per coil (~10 Hz).

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
cd /path/to/softMagneticComposite-Controller
pio run -t upload
pio device monitor   # 115200
```

Board env: `weact_mini_h743vitx` (STM32Cube). Useful `build_flags` in `platformio.ini`: `NUM_COILS`, `PWM_TARGET_HZ`, `HSE_VALUE`.

### ESP32-S3 bridge

```bash
pio run -d esp32_bridge -t upload
```

WiFi SSID/password and UART pins: `esp32_bridge/include/config.h`.

## Web UI

1. Join SoftAP **`CoilControl`** / **`coilcontrol`**
2. Open **http://192.168.4.1/**
3. Tabs:
   - **Field** — start/stop rotating field, shared freq/amp, CW/CCW, soft limit. Waveform/status strip follow **live** amp when the field is derated.
   - **DC coils** — per-coil constant drive. M1–M4 and **contour / program** lock while rotate is on (STOP first).
   - **Logs** — command traffic + raw CLI
4. Sticky **ALL OFF** in the header (`rotate off` then `off all`)
5. Link pill is **linked** only after a parsed `json` coil snapshot. Ping-only shows **UART up · status stale**.

## Serial CLI (STM32)

Both USART1 and USART3 accept the same commands (`help` for the full list). Examples:

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
rotate on|off
freq 5
amp 180
rot_dir cw|ccw
```

### Rotating field

```text
freq 2.5
amp 160
rot_dir cw
rotate on
...
rotate off
```

DC writes to M1–M4 (or CLI `contour` / `program`) automatically stop rotate and return those coils to safe DC control. `off all` / `program stop` also stop rotate and clear contour. **`rotate off` alone** disables M1–M4 and clears the contour flag.

`status` / `json` report commanded `amp` and soft-clamped `amp_live`. Incomplete CLI lines print usage; unknown motor tokens print an error.

## Project layout

```text
├── platformio.ini          # STM32H743 env
├── include/
│   ├── st7735.h            # Onboard TFT driver API
│   └── display_ui.h        # Live coil status layout
├── src/
│   ├── main.c              # Coil PWM, sine ISR, ADC, dual-UART CLI
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
