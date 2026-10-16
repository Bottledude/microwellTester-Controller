# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Firmware for a four-coil soft-magnetic field rig: an STM32H743 (STM32Cube HAL, plain C) drives the coils through a PCA9685 and BTS7960 bridges. An optional ESP32-S3 (Arduino) serves a WiFi web UI that forwards commands to the STM32 CLI. `README.md` is the full reference for wiring, pin maps, CLI commands, the web UI, Teleplot series names and field equations. Read it before changing hardware-facing behavior.

## Commands

PlatformIO (prefer `~/.platformio/penv/bin/pio` or the VS Code PIO install over distro packages).

- STM32 build / flash / monitor: `pio run`, `pio run -t upload`, `pio device monitor` (115200, USART1)
- ESP32 bridge: `pio run -d esp32_bridge` / `pio run -d esp32_bridge -t upload`
- Host math tests (no hardware): `bash monitor/run_field_tests.sh`. It runs the following, which can also be run on their own:
  - `python3 monitor/test_field_waveforms.py` (also checks the firmware `SINE_LUT` and `FIELD_CH`/`FIELD_CV` match the Python model)
  - `python3 monitor/test_dualsense_queue.py` (DualSense command queue, no pad or serial needed)
  - `gcc -O2 -std=c11 -Wall -Wextra -Iinclude -o /tmp/test_field_tilt monitor/test_field_tilt.c src/field_tilt.c -lm && /tmp/test_field_tilt`
  - The C test needs a host `gcc`. On the author's Windows machine it isn't on the Git Bash PATH (PlatformIO's ARM toolchain can't run the test), so only the Python half runs there.
- DualSense host mapper: `pip install -r monitor/requirements-gamepad.txt`, then `python monitor/dualsense_field.py --list | --port COMx | --dry-run`

There is no lint config, and `test/` (PlatformIO unit tests) is empty. "Tests" means the host scripts above.

## Architecture

**Two firmware projects, one command language.** The root `platformio.ini` builds the STM32 firmware (`src/`, `include/`). `esp32_bridge/` is a separate, nested PlatformIO project. The STM32 runs one CLI parser (`CLI_HandleLine` in `src/main.c`) on two UARTs:
- USART1 (PA9/PA10): the human Serial Monitor. It echoes input and carries Teleplot output. The DualSense script also writes here, so it and `pio device monitor` cannot both have the port open.
- USART3 (PD8/PD9): the ESP32 bridge, without echo. The ESP32 (`esp32_bridge/src/main.cpp`) exposes `GET /api/status` (it polls `ping`/`json`) and `POST /api/cmd`, which forwards a raw CLI string. The web UI (`esp32_bridge/include/www_page.h`, embedded HTML/JS) and `monitor/dualsense_field.py` are both just CLI clients.

So a new feature normally means adding a CLI verb in `main.c`, updating `CLI_PrintHelp`, adding any new state to `status`/`json`, and then wiring the web UI, the DualSense map and the README if needed. The ESP32 parses the `json` reply with ArduinoJson, so keep it a single line and keep the existing keys stable.

**`src/main.c` is the whole STM32 application** (~3k lines: HAL init, PCA9685, ADC current sense, CLI, ramps, MLX polling, Teleplot). The other `src/` files are small drivers (`st7735`, `display_ui`, `mlx90393`) plus `field_tilt.c`.

**Timing split:**
- TIM6 ISR at 1 kHz (`HAL_TIM_PeriodElapsedCallback`): advances `g_phase`, computes signed duties for M1–M4 from the 360-entry sine LUT and writes the shadow `g_pwm_r/l[]`, setting `g_pca_dirty`. It never touches I2C.
- Main loop: `CLI_Poll` → `PCA9685_Flush` (pushes the shadows over I2C1) → `Mlx_PollAll` (I2C2) → `CurrentLoop_Update` at 200 Hz (IS sensing, closed loop, soft current limit, and `Rotate_RampUpdate`) → Teleplot at 50 Hz → TFT at 10 Hz.
- Anything slow in the main loop delays PCA flushes. That is why Teleplot is off by default and the `json` handler flushes between ADC reads.

**Commanded vs live state.** Operator setpoints (`g_field_prog_want`, `g_rotate_dir_want`, `g_tilt_gamma_want`, `g_rotate_amp`) are kept separate from what the ISR uses (`g_field_prog`, `g_rotate_dir`, `g_tilt_*`, `g_rotate_amp_live`). `Rotate_RampUpdate` slews `amp_live` toward the target and only commits a program, direction or gamma change once live amplitude has faded through 0. Revolve start and stop also freeze phase (`g_revolve_phase_hold`) and shape the ramp with a quarter-sine envelope. Keep the ramp semantics when adding field modes. Only the "immediate cut" paths (`off all`, DC writes, `contour`, `program`) bypass the ramp.

**Field math lives in three places that must agree:**
1. `src/field_tilt.c` / `include/field_tilt.h`: pure C, no HAL. 30° coil tilt (`c_h=√3/2`, `c_v=1/2`), the forward map, the equal-V inverse, the shared scale and MLX centre reconstruction. It is compiled both into the firmware and into `monitor/test_field_tilt.c`.
2. The ISR and its helpers in `main.c` (`sine_scaled`, `revolve_tilt_currents`, `revolve_corner_phase`, the LUT indexing).
3. `monitor/test_field_waveforms.py`: a Python re-implementation of the LUT, quantization and rotate/revolve formulas.

If you change coil geometry, signs, LUT size, rounding or the corner definitions, update all three and run `run_field_tests.sh`.

**MLX90393 is display-only.** The four magnetometers on I2C2 feed `json`, Teleplot and the TFT through `field_tilt_reconstruct_center`. They never feed back into PWM. Coil output is always the open-loop geometric law (plus the optional IS-current closed loop in DC mode). Keep the PCA9685 (I2C1) and the MLX chips (I2C2) on separate buses.

## Constraints and conventions
- `NUM_COILS` must stay 4. It is set in the root `platformio.ini` build flags and again in `esp32_bridge/include/config.h`.
- Duty commands are 0–255 (signed −255..255 for bipolar drive), mapped to 12-bit on the PCA. `PWM_TARGET_HZ` is the PCA carrier, ~1 kHz, max ~1.5 kHz.
- `-Wl,-u,_printf_float` is required. `status`/`json` print floats through newlib-nano.
- Lab frame: Y is vertical. X and Z are horizontal. M1/M3 are the YZ pair, M2/M4 the XY pair.
- `/* #region agent log */ … /* #endregion */` blocks in `main.c` (the `g_dbg_*` counters, `dbg_print_snapshot`, `DBG ` lines) and the `monitor_filters = debuglog` filter (`monitor/filter_debuglog.py` → `.cursor/debug-*.log`) are temporary Cursor debug instrumentation. Leave them alone unless asked to remove them, and don't build new features on them.
- Flash both boards when the CLI or JSON contract changes.
