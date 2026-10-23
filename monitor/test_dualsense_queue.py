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
        self.axes = [0.0, 0.0, 0.0, 0.0, -1.0, -1.0]
        self.buttons = [0] * 16
        self.hat = (0, 0)

    def get_numbuttons(self) -> int:
        return 16

    def get_button(self, i: int) -> int:
        if i < 0 or i >= len(self.buttons):
            return 0
        return self.buttons[i]

    def get_numaxes(self) -> int:
        return len(self.axes)

    def get_axis(self, i: int) -> float:
        return self.axes[i]

    def get_numhats(self) -> int:
        return 1

    def get_hat(self, _i: int) -> tuple[int, int]:
        return self.hat


def fake_pad() -> ds.Pad:
    pad = ds.Pad.__new__(ds.Pad)
    pad.js = FakeJoystick()
    pad.btns = ds.BTN_SDL
    pad._prev_btn = [0] * 16
    pad._axis_prog = "y"
    pad._gamma = 45.0
    pad._amp = 0
    pad._freq = 1.0
    pad._fb = False
    pad._aim = None
    pad._adj_key = None
    pad._adj_wait = 0.0
    pad.pump = lambda: None
    return pad


def cmds(pad: ds.Pad, dt: float = 0.02) -> list[str]:
    return [c for c, _ in pad.collect(dt, False)]


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
    pad.js.buttons[ds.BTN_SDL["square"]] = 1
    if cmds(pad) != ["axis y"]:
        fail("square should select axis y")
    pad.js.buttons[ds.BTN_SDL["square"]] = 0
    pad.collect(0.02, False)
    pad.js.buttons[ds.BTN_SDL["cross"]] = 1
    if cmds(pad) != ["axis x"]:
        fail("cross should select axis x")
    pad.js.buttons[ds.BTN_SDL["cross"]] = 0
    pad.collect(0.02, False)
    pad.js.buttons[ds.BTN_SDL["circle"]] = 1
    if cmds(pad) != ["axis z"]:
        fail("circle should select axis z")
    pad.js.buttons[ds.BTN_SDL["circle"]] = 0
    pad.collect(0.02, False)
    pad.js.buttons[ds.BTN_SDL["triangle"]] = 1
    if cmds(pad) != ["axis tilt"]:
        fail("triangle should select axis tilt")
    print("ok: face buttons select y/x/z/tilt")

    pad = fake_pad()
    pad.js.hat = (0, 1)
    if cmds(pad) != []:
        fail("d-pad up without a trigger should do nothing")
    pad.js.axes[4] = 1.0  # L2
    first = cmds(pad, 0.02)
    if first != ["amp 5"]:
        fail(f"L2 + up should step amp, got {first}")
    if cmds(pad, 0.02) != []:
        fail("amp should not repeat before the hold delay")
    nxt = cmds(pad, 0.40)
    if nxt != ["amp 10"]:
        fail(f"held L2 + up should repeat amp, got {nxt}")
    print("ok: L2 + d-pad steps amp, hold repeats")

    pad = fake_pad()
    pad.js.axes[5] = 1.0  # R2
    pad.js.hat = (0, -1)
    got = cmds(pad)
    if got != ["gamma 40"]:
        fail(f"R2 + down should step gamma, got {got}")
    print("ok: R2 + d-pad steps gamma")

    pad = fake_pad()
    pad.js.axes[4] = 1.0
    pad.js.axes[5] = 1.0
    pad.js.hat = (0, 1)
    got = cmds(pad)
    if got != ["freq 1.25"]:
        fail(f"L2+R2 + up should step freq, got {got}")
    print("ok: both triggers + d-pad step freq")

    pad = fake_pad()
    pad.js.axes[2] = 1.0  # stick right
    got = cmds(pad)
    if got != ["aim 0"]:
        fail(f"stick right should aim 0, got {got}")
    if cmds(pad) != []:
        fail("aim should not resend an unchanged angle")
    pad.js.axes[2] = 0.0
    pad.js.axes[3] = -1.0  # stick up
    got = cmds(pad)
    if got != ["aim 90"]:
        fail(f"stick up should aim 90, got {got}")
    pad.js.axes[3] = 0.0
    got = cmds(pad)
    if got != ["aim run"]:
        fail(f"stick center should resume rotation, got {got}")
    if cmds(pad) != []:
        fail("aim run should be sent once")
    pad.js.axes[2] = -1.0
    got = cmds(pad)
    if got != ["aim 180"]:
        fail(f"stick left should aim 180, got {got}")
    pad.js.axes[2] = 0.0
    pad.js.axes[3] = 1.0
    got = cmds(pad)
    if got != ["aim 270"]:
        fail(f"stick down should aim 270, got {got}")
    print("ok: right stick aims the field and center resumes")

    pad = fake_pad()
    pad.js.buttons[ds.BTN_SDL["l3"]] = 1
    if cmds(pad) != ["fb on"]:
        fail("L3 should turn fb on")
    pad.js.buttons[ds.BTN_SDL["l3"]] = 0
    pad.collect(0.02, False)
    pad.js.buttons[ds.BTN_SDL["l3"]] = 1
    if cmds(pad) != ["fb off"]:
        fail("L3 should turn fb off")
    print("ok: L3 toggles MLX feedback")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
