#!/usr/bin/env python3
"""Drive the STM32 coil CLI from a DualSense on this PC (USB or Bluetooth).

The pad never talks to the H743 directly. This process is the HID host and
writes the same USART1 commands as the Serial Monitor.

  pip install -r monitor/requirements-gamepad.txt
  python monitor/dualsense_field.py --list
  python monitor/dualsense_field.py --port COM5
  python monitor/dualsense_field.py --dry-run

This script owns the Serial Monitor port. Close pio device monitor / Teleplot
on USART1 first. ESP32 USART3 can stay up in parallel.

Mapping (firmware γ is the field axis in XZ, 0–90°; changes while running
ramp amp through 0 — analog values are quantized and sent only on change):

  D-pad Up/Left/Right/Down   axis y / x / z / tilt
  Options                    start (rotate on / revolve <axis>)
  Create                     stop  (rotate off)
  PS                         ALL OFF (off all — immediate)
  L1 / R1                    rot_dir ccw / cw
  Square / Cross / Circle / Triangle   corner Q0 / Q1 / Q2 / Q3
  Left stick X               gamma 0..90 (deadzone holds last)
  Right stick X              amp 0..255 (deadzone holds last)
  Right stick Y              slew freq while held
  Touchpad click             status
"""
from __future__ import annotations

import argparse
import os
import sys
import threading
import time

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_JOYSTICK_HIDAPI_PS5", "1")

BAUD = 115200
DEAD = 0.25
GAMMA_STEP = 5.0
AMP_STEP = 5
FREQ_MIN, FREQ_MAX = 0.01, 50.0
FREQ_SLEW_HZ_PER_S = 4.0
CMD_GAP_S = 0.08
POLL_S = 0.02

PAD_HINTS = (
    "dualsense",
    "ps5",
    "wireless controller",
    "sony",
    "playstation",
)
PORT_HINTS = (
    "stmicro",
    "stm32",
    "weact",
    "ch340",
    "cp210",
    "usb serial",
    "usb-serial",
    "usb uart",
    "serial device",
)

# SDL GameController (Xbox layout). DualSense via pygame HIDAPI uses this.
BTN_SDL = {
    "cross": 0,
    "circle": 1,
    "square": 2,
    "triangle": 3,
    "create": 4,
    "ps": 5,
    "options": 6,
    "l3": 7,
    "r3": 8,
    "l1": 9,
    "r1": 10,
    "dpad_up": 11,
    "dpad_down": 12,
    "dpad_left": 13,
    "dpad_right": 14,
    "touchpad": 15,
}
# Sony DirectInput order seen on some Windows stacks.
BTN_SONY = {
    "square": 0,
    "cross": 1,
    "circle": 2,
    "triangle": 3,
    "l1": 4,
    "r1": 5,
    "l2": 6,
    "r2": 7,
    "create": 8,
    "options": 9,
    "l3": 10,
    "r3": 11,
    "ps": 12,
    "touchpad": 13,
    "mute": 14,
}


def _need(mod: str, pip: str) -> None:
    print(f"Missing {mod}. Install with:  pip install {pip}", file=sys.stderr)
    sys.exit(2)


try:
    import serial
    from serial.tools import list_ports
except ImportError:
    _need("pyserial", "pygame pyserial")

pygame = None  # loaded when the pad is opened


def _import_pygame():
    global pygame
    if pygame is not None:
        return pygame
    try:
        import pygame as pg
    except ImportError:
        _need("pygame", "pygame pyserial")
    pygame = pg
    return pg


def list_serial_ports() -> list:
    return list(list_ports.comports())


def _port_blob(p) -> str:
    return f"{p.device} {p.description} {p.hwid}".lower()


def _port_score(p) -> int:
    blob = _port_blob(p)
    if "bluetooth" in blob or "bthenum" in blob:
        return -10
    score = 0
    for h in PORT_HINTS:
        if h in blob:
            score += 2
    return score


def pick_serial_port(explicit: str | None) -> str:
    ports = list_serial_ports()
    if explicit:
        return explicit
    if not ports:
        raise SystemExit("No serial ports. Plug in the H743 USB-UART or pass --port.")
    scored = sorted(((_port_score(p), p) for p in ports), key=lambda t: t[0], reverse=True)
    best_score, best = scored[0]
    if best_score <= 0:
        print("Could not auto-pick the STM32 USART1 adapter.")
        print("DualSense Bluetooth often appears as extra COM ports - those are not the H743.\n")
        print_ports()
        raise SystemExit("Pass --port COMx for the USB-UART Serial Monitor.")
    return best.device


def print_ports() -> None:
    ports = list_serial_ports()
    if not ports:
        print("No serial ports.")
        return
    print("Serial ports:")
    for p in ports:
        note = ""
        blob = _port_blob(p)
        if "bluetooth" in blob or "bthenum" in blob:
            note = "  (Bluetooth SPP - not STM32 USART1)"
        print(f"  {p.device:12}  {p.description}{note}")


class Stm32Cli:
    def __init__(self, port: str | None, baud: int, dry: bool) -> None:
        self.dry = dry
        self._ser = None
        self._last_tx = 0.0
        self._rx_stop = threading.Event()
        self._rx_thr: threading.Thread | None = None
        if dry:
            print("[dry-run] not opening serial")
            return
        if not port:
            raise SystemExit("internal: serial port required")
        self._ser = serial.Serial(port, baud, timeout=0.05, write_timeout=0.5)
        time.sleep(0.15)
        self._ser.reset_input_buffer()
        self._rx_thr = threading.Thread(target=self._rx_loop, daemon=True)
        self._rx_thr.start()
        print(f"USART1 {port} @ {baud}")
        self.send("ping", urgent=True)
        time.sleep(0.2)

    def _rx_loop(self) -> None:
        assert self._ser is not None
        buf = ""
        while not self._rx_stop.is_set():
            try:
                n = self._ser.in_waiting
                chunk = self._ser.read(n or 1).decode("ascii", errors="replace")
            except serial.SerialException:
                break
            if not chunk:
                continue
            buf += chunk
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip("\r")
                if line:
                    print(f"  stm32< {line}")

    def send(self, cmd: str, urgent: bool = False) -> bool:
        cmd = cmd.strip()
        if not cmd:
            return True
        now = time.monotonic()
        if not urgent and (now - self._last_tx) < CMD_GAP_S:
            return False
        self._last_tx = now
        if self.dry:
            print(f"  cli> {cmd}")
            return True
        assert self._ser is not None
        try:
            self._ser.write((cmd + "\r\n").encode("ascii"))
            self._ser.flush()
        except serial.SerialException as e:
            print(f"serial write failed: {e}", file=sys.stderr)
            return True
        print(f"  cli> {cmd}")
        return True

    def close(self, stop_field: bool) -> None:
        if stop_field:
            try:
                self.send("rotate off", urgent=True)
                time.sleep(0.05)
            except Exception:
                pass
        self._rx_stop.set()
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass


class Pad:
    def __init__(self, layout: str) -> None:
        pg = _import_pygame()
        pg.init()
        pg.joystick.init()
        n = pg.joystick.get_count()
        if n <= 0:
            raise SystemExit(
                "No gamepad. Pair or plug the DualSense, then rerun.\n"
                "Windows: Settings → Bluetooth → DualSense, or USB cable."
            )
        idx = 0
        names = []
        for i in range(n):
            js = pg.joystick.Joystick(i)
            js.init()
            names.append(js.get_name())
            blob = js.get_name().lower()
            if any(h in blob for h in PAD_HINTS):
                idx = i
        self.js = pg.joystick.Joystick(idx)
        self.js.init()
        self.layout = layout
        self.btns = BTN_SONY if layout == "sony" else BTN_SDL
        print(f"Pad [{idx}/{n}]: {self.js.get_name()}")
        if n > 1:
            print("  others: " + ", ".join(f"{i}:{nm}" for i, nm in enumerate(names)))
        print(
            f"  buttons={self.js.get_numbuttons()} axes={self.js.get_numaxes()} "
            f"hats={self.js.get_numhats()} layout={layout}"
        )
        self._prev_btn = [0] * max(self.js.get_numbuttons(), 16)
        self._prev_hat = (0, 0)
        self._axis_prog = "y"
        self._gamma = 45.0
        self._amp: int | None = None
        self._freq = 1.0
        self._freq_sent: float | None = None

    def pump(self) -> None:
        _import_pygame().event.pump()

    def _btn(self, name: str) -> int:
        i = self.btns.get(name, -1)
        if i < 0 or i >= self.js.get_numbuttons():
            return 0
        return 1 if self.js.get_button(i) else 0

    def _edge(self, name: str) -> bool:
        i = self.btns.get(name, -1)
        if i < 0:
            return False
        cur = self._btn(name)
        prev = self._prev_btn[i] if i < len(self._prev_btn) else 0
        if i >= len(self._prev_btn):
            self._prev_btn.extend([0] * (i - len(self._prev_btn) + 1))
        self._prev_btn[i] = cur
        return cur == 1 and prev == 0

    def _axis(self, i: int) -> float:
        if i < 0 or i >= self.js.get_numaxes():
            return 0.0
        v = float(self.js.get_axis(i))
        if v > 1.0:
            v = 1.0
        if v < -1.0:
            v = -1.0
        return v

    def _hat(self) -> tuple[int, int]:
        if self.js.get_numhats() < 1:
            return (0, 0)
        return self.js.get_hat(0)

    def collect(self, dt: float, debug: bool) -> list[tuple[str, bool]]:
        """Return (cmd, urgent) edges + analog holds for this frame."""
        out: list[tuple[str, bool]] = []
        self.pump()

        if debug:
            pressed = [i for i in range(self.js.get_numbuttons()) if self.js.get_button(i)]
            if pressed:
                print(f"  buttons {pressed} hat={self._hat()} "
                      f"axes={[round(self._axis(i), 2) for i in range(self.js.get_numaxes())]}")

        hat = self._hat()
        if hat != self._prev_hat:
            hx, hy = hat
            if hy == 1:
                self._axis_prog = "y"
                out.append(("axis y", False))
            elif hx == -1:
                self._axis_prog = "x"
                out.append(("axis x", False))
            elif hx == 1:
                self._axis_prog = "z"
                out.append(("axis z", False))
            elif hy == -1:
                self._axis_prog = "tilt"
                out.append(("axis tilt", False))
            self._prev_hat = hat

        dpad_map = (
            ("dpad_up", "y"),
            ("dpad_left", "x"),
            ("dpad_right", "z"),
            ("dpad_down", "tilt"),
        )
        if self.js.get_numhats() < 1:
            for name, ax in dpad_map:
                if self._edge(name):
                    self._axis_prog = ax
                    out.append((f"axis {ax}", False))
        else:
            for name, _ax in dpad_map:
                self._edge(name)

        if self._edge("options"):
            if self._axis_prog == "y":
                out.append(("rotate on", False))
            elif self._axis_prog == "tilt":
                out.append(("revolve tilt", False))
            else:
                out.append((f"revolve {self._axis_prog}", False))
        if self._edge("create"):
            out.append(("rotate off", False))
        if self._edge("ps"):
            out.append(("off all", True))
        if self._edge("l1"):
            out.append(("rot_dir ccw", False))
        if self._edge("r1"):
            out.append(("rot_dir cw", False))
        if self._edge("square"):
            out.append(("corner Q0", False))
        if self._edge("cross"):
            out.append(("corner Q1", False))
        if self._edge("circle"):
            out.append(("corner Q2", False))
        if self._edge("triangle"):
            out.append(("corner Q3", False))
        if self._edge("touchpad"):
            out.append(("status", False))

        lx = self._axis(0)
        if abs(lx) >= DEAD:
            g = (lx + 1.0) * 45.0
            g = round(g / GAMMA_STEP) * GAMMA_STEP
            if g < 0.0:
                g = 0.0
            if g > 90.0:
                g = 90.0
            if abs(g - self._gamma) >= (GAMMA_STEP - 0.01):
                self._gamma = g
                out.append((f"gamma {g:.0f}", False))

        rx = self._axis(2) if self.js.get_numaxes() > 2 else 0.0
        if abs(rx) >= DEAD:
            a = int(round((rx + 1.0) * 0.5 * 255.0 / AMP_STEP) * AMP_STEP)
            if a < 0:
                a = 0
            if a > 255:
                a = 255
            if self._amp is None or a != self._amp:
                self._amp = a
                out.append((f"amp {a}", False))

        ry = self._axis(3) if self.js.get_numaxes() > 3 else 0.0
        if abs(ry) >= DEAD:
            # Stick Y is usually inverted (up = -1).
            self._freq += (-ry) * FREQ_SLEW_HZ_PER_S * dt
            if self._freq < FREQ_MIN:
                self._freq = FREQ_MIN
            if self._freq > FREQ_MAX:
                self._freq = FREQ_MAX
            q = round(self._freq * 4.0) / 4.0
            if q < FREQ_MIN:
                q = FREQ_MIN
            if q != self._freq_sent:
                self._freq_sent = q
                out.append((f"freq {q:.2f}", False))

        return out


def enqueue(pending: list[tuple[str, bool]], new: list[tuple[str, bool]]) -> None:
    """Queue commands. Newest non-urgent verb wins; urgent drops older queue."""
    for cmd, urgent in new:
        if urgent:
            pending.clear()
        else:
            verb = cmd.split()[0]
            pending[:] = [p for p in pending if p[1] or p[0].split()[0] != verb]
        pending.append((cmd, urgent))


def print_map() -> None:
    print(
        """
Controls
  D-pad Up      axis y     rotate Y (XZ)
  D-pad Left    axis x     revolve X (YZ, γ=0)
  D-pad Right   axis z     revolve Z (XY, γ=90)
  D-pad Down    axis tilt  revolve tilt (live γ)
  Options       START      rotate on / revolve <axis>
  Create        STOP       rotate off (ramp)
  PS            ALL OFF    off all (immediate)
  L1 / R1       rot_dir    ccw / cw
  □ / ✕ / ○ / △  corner    Q0 / Q1 / Q2 / Q3
  Left stick X  gamma      0° … 90°  (rest = hold)
  Right stick X amp        0 … 255   (rest = hold)
  Right stick Y freq       slew 0.01–50 Hz
  Touchpad      status
"""
    )


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    ap.add_argument("--port", help="USART1 COM port (auto if omitted)")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--dry-run", action="store_true", help="print CLI, do not open serial")
    ap.add_argument("--debug", action="store_true", help="print raw button/axis numbers")
    ap.add_argument(
        "--layout",
        choices=("sdl", "sony"),
        default="sdl",
        help="button map if D-pad/face buttons are swapped (default sdl)",
    )
    ap.add_argument(
        "--no-stop",
        action="store_true",
        help="do not send rotate off on exit",
    )
    args = ap.parse_args(argv)

    if args.list:
        print_ports()
        return 0

    print_map()
    pad = Pad(args.layout)
    port = None if args.dry_run else pick_serial_port(args.port)
    if not args.dry_run:
        print_ports()
        print(f"Using {port}")
    cli = Stm32Cli(port, args.baud, args.dry_run)

    print("Running. Ctrl+C to quit.")
    last = time.monotonic()
    pending: list[tuple[str, bool]] = []
    try:
        while True:
            now = time.monotonic()
            dt = now - last
            last = now
            enqueue(pending, pad.collect(dt, args.debug))
            pending.sort(key=lambda t: 0 if t[1] else 1)
            if pending:
                cmd, urgent = pending[0]
                if cli.send(cmd, urgent=urgent):
                    pending.pop(0)
            time.sleep(POLL_S)
    except KeyboardInterrupt:
        print("\nexit")
    finally:
        cli.close(stop_field=not args.no_stop and not args.dry_run)
        if pygame is not None:
            pygame.quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
