# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Firmware for a four-coil soft-magnetic field rig: one **ESP32-WROOM-32** (Arduino) drives the coils through a PCA9685 and BTS7960 bridges, serves the WiFi web UI, pairs a DualSense over Bluetooth Classic, and polls four display-only MLX90393 chips. There is no STM32 and no current sense. `README.md` is the full reference for wiring, pin maps, CLI commands, the web UI, Teleplot series names and field equations. Read it before changing hardware-facing behavior.

## Commands

PlatformIO (prefer `~/.platformio/penv/bin/pio` or the VS Code PIO install over distro packages).

- Build / flash / monitor: `pio run`, `pio run -t upload`, `pio device monitor` (115200, UART0 USB)
- Host math tests (no hardware): `bash monitor/run_field_tests.sh`. It runs the following, which can also be run on their own:
  - `python3 monitor/test_field_waveforms.py` (also checks the firmware `SINE_LUT` and `FIELD_CH`/`FIELD_CV` match the Python model)
  - `python3 monitor/test_dualsense_queue.py` (DualSense command queue, no pad or serial needed)
  - `gcc -O2 -std=c11 -Wall -Wextra -Iinclude -o /tmp/test_field_tilt monitor/test_field_tilt.c src/field_tilt.c -lm && /tmp/test_field_tilt`
  - `gcc -O2 -std=c11 -Wall -Wextra -Iinclude -o /tmp/test_field_fb monitor/test_field_fb.c src/field_fb.c src/field_tilt.c -lm && /tmp/test_field_fb` (MLX feedback on a simulated rig)
  - The C tests need a host compiler. On the author's Windows machine `gcc` isn't on the Git Bash PATH, but MSVC `cl` works (vcvars64; build from a short path, since cmd can't use the repo path with `!`).
- DualSense is paired to the ESP32 (Bluepad32). Optional UART fallback: `pip install -r monitor/requirements-gamepad.txt`, then `python monitor/dualsense_field.py --list | --port COMx | --dry-run`

There is no lint config. "Tests" means the host scripts above.

## Architecture

**One firmware, one command language.** The root `platformio.ini` builds `esp32dev` (`src/`, `include/`). `CLI_HandleLine` in `src/cli.c` is called in-process by UART0, `POST /api/cmd`, and DualSense queue drains.

A new feature normally means adding a CLI verb in `cli.c`, updating `CLI_PrintHelp`, adding any new state to `status`/`json`, and then wiring the web UI (`include/www_page.h`), the DualSense map (`src/pad.cpp`) and the README if needed. Keep `json` a single line and keep the existing keys stable.

**Timing split:**
- Hardware timer at 1 kHz notifies a core-1 sample task (`Field_SampleTick` in `src/field_engine.c`): advances `g_phase`, computes signed duties for M1–M4 from the 360-entry sine LUT and writes the shadow `g_pwm_r/l[]`, setting `g_pca_dirty`. It never touches I2C.
- Core-1 flush task: `PCA9685_Flush` (I2C0) → `Rotate_RampUpdate` at 200 Hz → `Mlx_PollAll` at 50 Hz (I2C1) → drain DualSense CLI queue.
- Core 0: WiFi SoftAP, web server, Bluepad32, UART0 CLI.
- Teleplot is off by default so UART does not starve PCA9685 flushes.

**Commanded vs live state.** Operator setpoints (`g_field_prog_want`, `g_rotate_dir_want`, `g_tilt_gamma_want`, `g_rotate_amp`) are kept separate from what the sample tick uses (`g_field_prog`, `g_rotate_dir`, `g_tilt_*`, `g_rotate_amp_live`). `Rotate_RampUpdate` slews `amp_live` toward the target and only commits a program, direction or gamma change once live amplitude has faded through 0. Revolve start and stop also freeze phase (`g_revolve_phase_hold`) and shape the ramp with a quarter-sine envelope. Keep the ramp semantics when adding field modes. Only the "immediate cut" paths (`off all`, DC writes, `contour`, `program`) bypass the ramp.

**Field math lives in three places that must agree:**
1. `src/field_tilt.c` / `include/field_tilt.h`: pure C, no Arduino. 30° coil tilt (`c_h=√3/2`, `c_v=1/2`), the forward map, the per-coil inverse (`field_tilt_weights`: a13/a24 for H, w13/w24 = ½ ∓ cos2γ/6 for V, every coil a pure sine), the max-circle gain 6/√(15+cos²2γ), the shared scale and MLX centre reconstruction. It is compiled both into the firmware and into `monitor/test_field_tilt.c`.
2. The sample tick and its helpers in `field_engine.c` (`sine_scaled`, `revolve_tilt_currents`, `revolve_corner_phase`, the LUT indexing).
3. `monitor/test_field_waveforms.py`: a Python re-implementation of the LUT, quantization and rotate/revolve formulas.

If you change coil geometry, signs, LUT size, rounding or the corner definitions, update all three and run `run_field_tests.sh`.

**MLX90393 is display-only unless `fb on`.** The four magnetometers on I2C1 feed `json`, Teleplot and the web UI through `field_tilt_center_from_sensors`. With `fb off` (default) coil output is the open-loop geometric law, bit-for-bit the old path. `fb on` enables the opt-in feedback:
- `src/field_fb.c` (pure C, host-tested by `monitor/test_field_fb.c`) fits each chip's axial reading against the drive phase and learns a complex per-coil correction K_i.
- `src/fb_app.c` runs it on the flush task from full chip sets that `Mlx_PollAll` tags with the trigger phase.
- `fb_corrected_sample` in `field_engine.c` drives coil i at |K_i|·u_i(θ + arg K_i).
- Learning only runs while `Field_FbSteady()` holds. K is stored in time convention and conjugated for CCW.
- New field programs must be expressible as α_i cosθ + β_i sinθ per coil (`field_coeffs`), or the feedback path must skip them.

Keep the PCA9685 (I2C0 / `Wire`) and the MLX chips (I2C1 / `Wire1`) on separate buses.

## Constraints and conventions
- `NUM_COILS` must stay 4. It is set in the root `platformio.ini` build flags and again in `include/config.h`.
- Duty commands are 0–255 (signed −255..255 for bipolar drive), mapped to 12-bit on the PCA. `PWM_TARGET_HZ` is the PCA carrier, ~1 kHz, max ~1.5 kHz.
- Lab frame: Y is vertical. X and Z are horizontal. M1/M3 are the YZ pair, M2/M4 the XY pair.
- DualSense uses Bluetooth Classic. Use an ESP32-WROOM-32, not an S3.
- ESP32 pins are not 5 V tolerant. Both I2C buses stay at 3.3 V.
