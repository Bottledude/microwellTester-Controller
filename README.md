# Soft Magnetic Composite Controller

PlatformIO firmware for driving soft-magnetic coils with a **WeAct Mini STM32H743VIT6** and up to six **BTS7960 / IBT-2** half-bridge drivers. An optional **ESP32-S3 N16R8** provides a WiFi SoftAP and browser UI while the STM32 Serial Monitor CLI remains available in parallel.

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
- **~40 kHz PWM**, duty **0–255** (maps to magnitude / I_ref scale; 255 ≈ 2 A)
- **Direction** (+/−) selects RPWM vs LPWM for B-field polarity
- **Open-loop** duty or **closed-loop** current via BTS7960 IS → ADC
- Soft current limits (`limit`)
- Dual CLI ports: human Serial Monitor + machine ESP32 bridge (`ping` / `json`)
- Web UI tabs: **Configuration**, **Motor drivers**, **Logs**

## Hardware

### Coil / BTS7960 pin map (STM32)

| Coil | RPWM | LPWM | EN | IS (ADC) |
|------|------|------|-----|----------|
| M1 | PE9 | PE11 | PE8 | PA0 |
| M2 | PE13 | PE14 | PE10 | PA1 |
| M3 | PC6 | PC7 | PC5 | PA2 |
| M4 | PC8 | PC9 | PC4 | PA3 |
| M5 | PA6 | PA7 | PB10 | PA4 |
| M6 | PB0 | PB1 | PB11 | PA5 |

IBT-2 / BTS7960: tie **R_EN + L_EN** to EN, logic **VCC = 5 V**, **VS** = coil supply. IS → STM32 ADC (see `IS_RIS_OHM` / `IS_K_RATIO` in `src/main.c`).

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
3. Use tabs:
   - **Configuration** — mode, current limits, default magnitude, programs
   - **Motor drivers** — per-coil enable / magnitude / direction
   - **Logs** — command traffic + raw CLI box

Link pill uses a `ping` → `PONG` heartbeat; coil readouts come from the STM32 `json` status line.

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
```

## Project layout

```text
├── platformio.ini          # STM32H743 env
├── src/main.c              # Coil PWM, ADC, dual-UART CLI
├── esp32_bridge/
│   ├── platformio.ini      # ESP32-S3 Arduino env
│   ├── include/config.h    # SoftAP + UART pins
│   ├── include/www_page.h  # Embedded web UI
│   └── src/main.cpp        # SoftAP, HTTP API, UART bridge
└── CoilController.code-workspace
```

## License / research use

Research firmware for soft-magnetic composite coil experiments (HKUST). Adapt pin maps and current-sense calibration for your hardware before continuous high-current operation.
