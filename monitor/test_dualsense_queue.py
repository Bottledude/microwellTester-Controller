#!/usr/bin/env python3
"""Host check of the DualSense command queue (no pad, no serial)."""
from __future__ import annotations

import sys
import types
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    import serial  # noqa: F401
except ImportError:  # queue logic does not need pyserial
    stub = types.ModuleType("serial")
    stub.tools = types.ModuleType("serial.tools")
    stub.tools.list_ports = types.ModuleType("serial.tools.list_ports")
    sys.modules["serial"] = stub
    sys.modules["serial.tools"] = stub.tools
    sys.modules["serial.tools.list_ports"] = stub.tools.list_ports

import dualsense_field as ds  # noqa: E402


def fail(msg: str) -> None:
    print("FAIL:", msg)
    sys.exit(1)


class FakeJoystick:
    def __init__(self) -> None:
        self.axes = [0.0, 0.0, 0.0, 0.0]

    def get_numbuttons(self) -> int:
        return 16

    def get_button(self, _i: int) -> int:
        return 0

    def get_numaxes(self) -> int:
        return len(self.axes)

    def get_axis(self, i: int) -> float:
        return self.axes[i]

    def get_numhats(self) -> int:
        return 1

    def get_hat(self, _i: int) -> tuple[int, int]:
        return (0, 0)


def fake_pad() -> ds.Pad:
    pad = ds.Pad.__new__(ds.Pad)
    pad.js = FakeJoystick()
    pad.btns = ds.BTN_SDL
    pad._prev_btn = [0] * 16
    pad._prev_hat = (0, 0)
    pad._axis_prog = "y"
    pad._gamma = 45.0
    pad._amp = None
    pad._freq = 1.0
    pad._freq_sent = None
    pad.pump = lambda: None
    return pad


def main() -> int:
    q: list[tuple[str, bool]] = []
    ds.enqueue(q, [("gamma 10", False)])
    ds.enqueue(q, [("gamma 15", False)])
    ds.enqueue(q, [("amp 50", False), ("gamma 20", False)])
    if q != [("amp 50", False), ("gamma 20", False)]:
        fail(f"newest gamma should win, got {q}")

    ds.enqueue(q, [("rotate on", False), ("rotate off", False)])
    if [c for c, _ in q if c.startswith("rotate")] != ["rotate off"]:
        fail(f"last rotate verb should win, got {q}")

    ds.enqueue(q, [("off all", True)])
    if q != [("off all", True)]:
        fail(f"ALL OFF must drop older queued commands, got {q}")
    ds.enqueue(q, [("corner Q1", False)])
    if q != [("off all", True), ("corner Q1", False)]:
        fail(f"commands after ALL OFF stay queued behind it, got {q}")
    print("ok: queue keeps newest value per verb; ALL OFF flushes older commands")

    pad = fake_pad()
    pad.js.axes[3] = -1.0  # right stick Y held up
    sent = []
    for _ in range(10):
        sent += [c for c, _ in pad.collect(0.001, False) if c.startswith("freq")]
    if len(sent) != len(set(sent)):
        fail(f"freq resent unchanged values: {sent}")
    if not sent:
        fail("freq slew sent nothing")
    print("ok: freq slew only sends changed values")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
