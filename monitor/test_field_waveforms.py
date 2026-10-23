#!/usr/bin/env python3
"""Host check of rotate-Y / four-coil 30° revolve-X/Z/tilt vs the lab-frame map."""
from __future__ import annotations

import math
import re
import sys
from pathlib import Path

CH = math.sqrt(3.0) / 2.0  # cos 30°
CV = 0.5  # sin 30°
INV_H = 1.0 / (2.0 * CH)
INV_V = 1.0 / (4.0 * CV)
N = 360
LUT_90 = 90


def lut_mod(idx: int) -> int:
    return idx % N


def lut_from_phase(ph: int) -> int:
    return (ph * N) >> 32


def sine_scaled(idx: int, amp: int) -> int:
    idx = lut_mod(idx)
    q15 = int(round(math.sin(2.0 * math.pi * idx / N) * 32767.0))
    if q15 > 32767:
        q15 = 32767
    if q15 < -32767:
        q15 = -32767
    return (q15 * amp) >> 15


def rotate_y(si: int, amp: int, ccw: bool) -> tuple[int, int, int, int]:
    d13 = sine_scaled(si + LUT_90, amp)
    d24 = sine_scaled(si, amp)
    if ccw:
        d24 = -d24
    return d13, d24, d13, d24  # M1, M2, M3, M4


def lut_unit(idx: int) -> float:
    """Match firmware SINE_LUT[i] / 32767."""
    idx = lut_mod(idx)
    q15 = int(round(math.sin(2.0 * math.pi * idx / N) * 32767.0))
    if q15 > 32767:
        q15 = 32767
    if q15 < -32767:
        q15 = -32767
    return q15 / 32767.0


def clamp_pwm(v: float) -> int:
    if v > 255.0:
        return 255
    if v < -255.0:
        return -255
    if v >= 0.0:
        return int(v + 0.5)
    return int(v - 0.5)


def lab_from_pair(b1: float, b2: float) -> tuple[float, float]:
    """Coil-to-lab map used for both XY (revolve z) and YZ (revolve x)."""
    horiz = -CH * (b1 + b2)
    vert = CV * (b1 - b2)
    return horiz, vert


def lab_b_from_coils(m1: float, m2: float, m3: float, m4: float) -> tuple[float, float, float, float]:
    """Lab Bx (XY H), By (V), Bz (YZ H), |B| from signed PWM proxies."""
    bx, by_xy = lab_from_pair(m2, m4)
    bz, by_yz = lab_from_pair(m1, m3)
    by = by_xy + by_yz
    babs = math.sqrt(bx * bx + by * by + bz * bz)
    return bx, by, bz, babs


def fail(msg: str) -> None:
    print("FAIL:", msg)
    sys.exit(1)


def tilt_sincos_gamma(deg: float) -> tuple[float, float]:
    if deg <= 0.0:
        return 0.0, 1.0
    if deg == 90.0:
        return 1.0, 0.0
    if deg >= 180.0:
        return 0.0, -1.0
    r = math.radians(deg)
    return math.sin(r), math.cos(r)


def tilt_weights(sg: float, cg: float) -> tuple[float, float, float, float]:
    """Match field_tilt_weights(): (a13, a24, w13, w24)."""
    cos2g = cg * cg - sg * sg
    wv = 1.0 / (4.0 * CV)
    dw = cos2g * CV / (4.0 * CH * CH)
    return cg / (2.0 * CH), sg / (2.0 * CH), wv - dw, wv + dw


def tilt_synthesize(H: float, V: float, sg: float, cg: float) -> tuple[float, float, float, float]:
    """Match field_tilt_synthesize(): one linear law per coil (pure sines)."""
    a13, a24, w13, w24 = tilt_weights(sg, cg)
    return (-a13 * H + w13 * V, a24 * H + w24 * V, -a13 * H - w13 * V, a24 * H - w24 * V)


def tilt_shared_scale(ah: float, av: float, sg: float, cg: float, umax: float = 255.0) -> float:
    a13, a24, w13, w24 = tilt_weights(sg, cg)
    pmax = max(math.hypot(a13 * ah, w13 * av), math.hypot(a24 * ah, w24 * av))
    if umax <= 0.0 or pmax <= umax:
        return 1.0
    return umax / pmax


def tilt_max_gain(sg: float, cg: float) -> float:
    """R_max / umax for a circle (H = V): 6 / sqrt(15 + cos^2 2γ)."""
    a13, a24, w13, w24 = tilt_weights(sg, cg)
    return 1.0 / math.sqrt(max(a13 * a13 + w13 * w13, a24 * a24 + w24 * w24))


def tilt_commands_exact(ah: float, av: float, sin_th: float, cos_th: float,
                        sg: float, cg: float, umax: float = 255.0):
    s = tilt_shared_scale(ah, av, sg, cg, umax)
    raw = tilt_synthesize((ah * s) * cos_th, (av * s) * sin_th, sg, cg)
    return raw, s


def tilt_commands_lut(phase: int, amp_h: int, amp_v: int, ccw: bool,
                      sg: float, cg: float) -> tuple[tuple[int, int, int, int], float]:
    """Match firmware revolve_tilt_currents() (LUT sin/cos, max gain, shared scale, signed round)."""
    ph = phase & 0xFFFFFFFF
    if ccw:
        ph = (-ph) & 0xFFFFFFFF
    si = lut_from_phase(ph)
    s = lut_unit(si)
    c = lut_unit(si + LUT_90)
    g = tilt_max_gain(sg, cg)
    raw, scale = tilt_commands_exact(float(amp_h) * g, float(amp_v) * g, s, c, sg, cg, 255.0)
    return (clamp_pwm(raw[0]), clamp_pwm(raw[1]), clamp_pwm(raw[2]), clamp_pwm(raw[3])), scale


def revolve_z(phase: int, amp_h: int, ccw: bool = False, amp_v: int | None = None) -> tuple[int, int, int, int]:
    if amp_v is None:
        amp_v = amp_h
    cmd, _ = tilt_commands_lut(phase, amp_h, amp_v, ccw, *tilt_sincos_gamma(90.0))
    return cmd


def revolve_x(phase: int, amp_h: int, ccw: bool = False, amp_v: int | None = None) -> tuple[int, int, int, int]:
    if amp_v is None:
        amp_v = amp_h
    cmd, _ = tilt_commands_lut(phase, amp_h, amp_v, ccw, *tilt_sincos_gamma(0.0))
    return cmd


def check_tilt_mode() -> None:
    A = 100.0
    tol = 1e-4
    sg, cg = tilt_sincos_gamma(0.0)
    raw, _ = tilt_commands_exact(A, A, 0.0, 1.0, sg, cg)
    if any(abs(a - b) > tol for a, b in zip(raw, (-57.735027, 0.0, -57.735027, 0.0))):
        fail(f"python gamma=0 theta=0 commands {raw}")
    bx, by, bz, _ = lab_b_from_coils(*raw)
    if abs(bx) > tol or abs(by) > tol or abs(bz - 100.0) > tol:
        fail(f"python gamma=0 theta=0 field {bx},{by},{bz}")

    sg, cg = tilt_sincos_gamma(45.0)
    raw, _ = tilt_commands_exact(A, A, 0.0, 1.0, sg, cg)
    if any(abs(a - b) > 1e-4 for a, b in zip(raw, (-40.824829, 40.824829, -40.824829, 40.824829))):
        fail(f"python gamma=45 theta=0 commands {raw}")
    bx, by, bz, _ = lab_b_from_coils(*raw)
    if abs(bx + 70.710678) > 1e-3 or abs(by) > tol or abs(bz - 70.710678) > 1e-3:
        fail(f"python gamma=45 theta=0 field {bx},{by},{bz}")

    sg, cg = tilt_sincos_gamma(90.0)
    raw, _ = tilt_commands_exact(A, A, 0.0, 1.0, sg, cg)
    if any(abs(a - b) > tol for a, b in zip(raw, (0.0, 57.735027, 0.0, 57.735027))):
        fail(f"python gamma=90 theta=0 commands {raw}")
    bx, by, bz, _ = lab_b_from_coils(*raw)
    if abs(bx + 100.0) > tol or abs(by) > tol or abs(bz) > tol:
        fail(f"python gamma=90 theta=0 field {bx},{by},{bz}")

    for deg in (0.0, 15.0, 30.0, 45.0, 60.0, 75.0, 90.0, 120.0, 150.0, 180.0):
        sg, cg = tilt_sincos_gamma(deg)
        raw, _ = tilt_commands_exact(A, A, 1.0, 0.0, sg, cg)
        # Pure vertical: w13 = 1/2 - cos2γ/6 on M1/M3, w24 = 1/2 + cos2γ/6 on M2/M4.
        c2 = math.cos(2.0 * math.radians(deg))
        w13 = 0.5 - c2 / 6.0
        w24 = 0.5 + c2 / 6.0
        want = (A * w13, A * w24, -A * w13, -A * w24)
        if any(abs(a - b) > 1e-4 for a, b in zip(raw, want)):
            fail(f"python theta=90 commands at gamma={deg}: {raw} want {want}")
        want_b = 100.0 * tilt_max_gain(sg, cg)
        for si in range(0, N, 5):
            ph = (si << 32) // N
            cmd, _ = tilt_commands_lut(ph, 100, 100, False, sg, cg)
            bx, by, bz, babs = lab_b_from_coils(*cmd)
            bn = bx * cg + bz * sg
            if abs(bn) > 2.0:
                fail(f"LUT Bn={bn} at gamma={deg} si={si}")
            if abs(babs - want_b) > 3.0:
                fail(f"LUT |B|={babs} at gamma={deg} si={si} want {want_b}")
            cmd_ccw, _ = tilt_commands_lut(ph, 100, 100, True, sg, cg)
            bx2, by2, bz2, babs2 = lab_b_from_coils(*cmd_ccw)
            if abs(bx2 * cg + bz2 * sg) > 2.0:
                fail("CCW left the tilt plane")
            if abs(babs2 - babs) > 3.0:
                fail("CCW changed |B|")

    sg, cg = tilt_sincos_gamma(60.0)
    cw, _ = tilt_commands_lut(0x10000000, 80, 80, False, sg, cg)
    ccw, _ = tilt_commands_lut(0x10000000, 80, 80, True, sg, cg)
    if cw == ccw:
        fail("CW and CCW commands identical at mid-quadrant")

    zcmd, _ = tilt_commands_lut(0, 0, 0, False, *tilt_sincos_gamma(45.0))
    if zcmd != (0, 0, 0, 0):
        fail(f"zero amp LUT commands {zcmd}")

    sg, cg = tilt_sincos_gamma(0.0)
    sc = tilt_shared_scale(510.0, 510.0, sg, cg, 255.0)
    if sc >= 1.0:
        fail("expected shared scale < 1 for Ah=Av=510")
    raw, _ = tilt_commands_exact(510.0, 510.0, 0.0, 1.0, sg, cg, 255.0)
    if max(abs(v) for v in raw) > 255.0 + 1e-4:
        fail("shared scale left a coil above Umax")

    # X/Z now share the 4-coil synth (gamma 0 / 90).
    if revolve_x(0, 100)[1] != 0 or revolve_x(0, 100)[3] != 0:
        fail("revolve x Q0 should leave M2/M4 at 0 (pure H on YZ pair)")
    if revolve_z(0, 100)[0] != 0 or revolve_z(0, 100)[2] != 0:
        fail("revolve z Q0 should leave M1/M3 at 0 (pure H on XY pair)")

    check_max_circle()


def check_max_circle() -> None:
    """amp 255 is the largest constant-|B| circle with pure-sine coils."""
    for deg in (0.0, 15.0, 30.0, 45.0, 60.0, 90.0, 120.0, 135.0, 180.0):
        sg, cg = tilt_sincos_gamma(deg)
        g = tilt_max_gain(sg, cg)
        closed = 6.0 / math.sqrt(15.0 + math.cos(2.0 * math.radians(deg)) ** 2)
        if abs(g - closed) > 1e-9:
            fail(f"gain at gamma={deg}: {g} vs closed form {closed}")
        want = 255.0 * g
        peaks = [0.0, 0.0, 0.0, 0.0]
        mags = []
        for i in range(720):
            th = 2.0 * math.pi * i / 720.0
            raw, sc = tilt_commands_exact(want, want, math.sin(th), math.cos(th), sg, cg)
            if abs(sc - 1.0) > 1e-6:
                fail(f"max circle at gamma={deg} should not need shared scale ({sc})")
            peaks = [max(p, abs(v)) for p, v in zip(peaks, raw)]
            bx, by, bz, babs = lab_b_from_coils(*raw)
            if abs(bx * cg + bz * sg) > 1e-6:
                fail(f"max circle left the plane at gamma={deg}")
            mags.append(babs)
        # All four coils reach ±255 together (no coil idles, none clips).
        if any(abs(p - 255.0) > 0.05 for p in peaks):
            fail(f"max circle at gamma={deg}: coil peaks {peaks} want all 255")
        if max(mags) - min(mags) > 1e-3 or abs(mags[0] - want) > 1e-3:
            fail(f"max circle |B| not constant at gamma={deg}: {min(mags)}..{max(mags)}")
        if tilt_shared_scale(want * 1.01, want * 1.01, sg, cg) >= 1.0:
            fail(f"gamma={deg}: circle above R_max was accepted")

    # Revolve X roles: M1/M3 = H + 1/3 V, M2/M4 = 2/3 V and no H.
    for si in range(0, N, 3):
        ph = (si << 32) // N
        th = 2.0 * math.pi * lut_from_phase(ph) / N
        m1, m2, m3, m4 = tilt_commands_lut(ph, 200, 200, False, 0.0, 1.0)[0]
        R = 200 * 1.5
        want = (-R / math.sqrt(3.0) * math.cos(th) + R / 3.0 * math.sin(th),
                2.0 * R / 3.0 * math.sin(th),
                -R / math.sqrt(3.0) * math.cos(th) - R / 3.0 * math.sin(th),
                -2.0 * R / 3.0 * math.sin(th))
        if any(abs(a - b) > 1.01 for a, b in zip((m1, m2, m3, m4), want)):
            fail(f"revolve x roles at si={si}: {(m1, m2, m3, m4)} want {want}")
        if abs(m2 + m4) > 1:
            fail(f"revolve x: M2/M4 must cancel horizontally at si={si} ({m2},{m4})")
        m1, m2, m3, m4 = revolve_z(ph, 200)
        if abs(m1 + m3) > 1:
            fail(f"revolve z: M1/M3 must cancel horizontally at si={si} ({m1},{m3})")

    # Revolve X at amp 255: every coil peaks at 255, |B| ≈ 1.5·255 throughout.
    mags = []
    peaks = [0, 0, 0, 0]
    for si in range(N):
        ph = (si << 32) // N
        cmd = revolve_x(ph, 255)
        peaks = [max(p, abs(v)) for p, v in zip(peaks, cmd)]
        mags.append(lab_b_from_coils(*cmd)[3])
    want = 255.0 * 1.5
    if any(p < 254 or p > 255 for p in peaks):
        fail(f"revolve x amp 255 coil peaks {peaks}, want all ~255")
    if max(abs(m - want) for m in mags) > 3.0:
        fail(f"revolve x amp 255 |B| {min(mags)}..{max(mags)} want ~{want}")
    print(f"ok: revolve X/Z max circle |B|={want:.1f}, all four coils pure sines at 255")


def envelope_amp(live: int, peak: int) -> int:
    """Match firmware revolve_envelope_amp()."""
    if peak == 0:
        return 0
    if live >= peak:
        return peak
    idx = (LUT_90 * live) // peak
    s = sine_scaled(idx, peak)
    if s < 0:
        s = 0
    if s > 255:
        s = 255
    return s


def check_firmware_constants() -> None:
    """The Python model must use the firmware's LUT and coil tilt, not a copy."""
    root = Path(__file__).resolve().parent.parent
    engine_c = (root / "src" / "field_engine.c").read_text(encoding="utf-8")
    cfg = (root / "include" / "config.h").read_text(encoding="utf-8")
    hdr = (root / "include" / "field_tilt.h").read_text(encoding="utf-8")

    m = re.search(r"#define\s+SINE_LUT_SIZE\s+(\d+)", cfg)
    if not m or int(m.group(1)) != N:
        fail(f"SINE_LUT_SIZE in config.h != {N}")
    m = re.search(r"#define\s+SINE_LUT_90\s+(\d+)u?", cfg)
    if not m or int(m.group(1)) != LUT_90:
        fail(f"SINE_LUT_90 in config.h != {LUT_90}")
    m = re.search(r"SINE_LUT\[SINE_LUT_SIZE\]\s*=\s*\{([^}]*)\}", engine_c)
    if not m:
        fail("SINE_LUT table not found in field_engine.c")
    fw = [int(v) for v in m.group(1).replace("\n", " ").split(",") if v.strip()]
    if len(fw) != N:
        fail(f"SINE_LUT has {len(fw)} entries, want {N}")
    for i, v in enumerate(fw):
        want = int(round(lut_unit(i) * 32767.0))
        if v != want:
            fail(f"SINE_LUT[{i}]={v} but Python model uses {want}")

    for name, want in (("FIELD_CH", CH), ("FIELD_CV", CV)):
        m = re.search(rf"#define\s+{name}\s+([0-9.]+)f", hdr)
        if not m or abs(float(m.group(1)) - want) > 1e-7:
            fail(f"{name} in field_tilt.h does not match Python {want}")


def main() -> int:
    check_firmware_constants()
    print("ok: Python model matches firmware SINE_LUT and 30 deg tilt constants")

    amp = 180

    babs_vals: list[float] = []
    for si in range(0, N, 8):
        m1, m2, m3, m4 = rotate_y(si, amp, False)
        if m1 != m3 or m2 != m4:
            fail(f"rotate Y pair mismatch at si={si}: {m1},{m2},{m3},{m4}")
        c = sine_scaled(si + LUT_90, amp)
        s = sine_scaled(si, amp)
        if m1 != c or m2 != s:
            fail(f"rotate Y expected cos/sin at si={si}: got {m1},{m2} want {c},{s}")
        _, m2c, _, m4c = rotate_y(si, amp, True)
        if m2c != -s or m4c != -s:
            fail(f"rotate Y CCW did not negate sin pair at si={si}")
        bx, by, bz, babs = lab_b_from_coils(m1, m2, m3, m4)
        if abs(by) > 1e-6:
            fail(f"rotate Y By should cancel at si={si}: By={by}")
        want_bx = -math.sqrt(3.0) * float(m2)
        want_bz = -math.sqrt(3.0) * float(m1)
        if abs(bx - want_bx) > 1e-4 or abs(bz - want_bz) > 1e-4:
            fail(f"rotate Y H split at si={si}: Bx,Bz={bx},{bz} want {want_bx},{want_bz}")
        babs_vals.append(babs)
        bx_ccw, _, bz_ccw, _ = lab_b_from_coils(m1, m2c, m3, m4c)
        if abs(bx_ccw + want_bx) > 1e-4:
            fail(f"rotate Y CCW should negate Bx at si={si}: {bx_ccw}")
        if abs(bz_ccw - want_bz) > 1e-4:
            fail(f"rotate Y CCW should keep Bz at si={si}: {bz_ccw}")

    babs_span = max(babs_vals) - min(babs_vals)
    if babs_span > 8.0:
        fail(f"rotate Y |B| not nearly constant: span {babs_span}")
    mean_rot = sum(babs_vals) / len(babs_vals)
    if abs(mean_rot - amp * math.sqrt(3.0)) > 5.0:
        fail(f"rotate Y |B| mean {mean_rot} want ~{amp * math.sqrt(3.0)}")

    # Cardinals via 4-coil synth. Q0 γ=90 is −X (+Bh in tilt frame).
    corners = [
        (0x00000000, "Bh+"),
        (0x40000000, "Bv+"),
        (0x80000000, "Bh-"),
        (0xC0000000, "Bv-"),
    ]
    z_signs = []
    x_signs = []
    q0_bh = q1_bv = None
    rev_b = amp * 1.5  # |B| = amp * max gain (1.5 for X/Z)
    for ph, name in corners:
        zcmd = revolve_z(ph, amp, False)
        bx, by, bz, babs = lab_b_from_coils(*zcmd)
        if abs(bz) > 1.0:
            fail(f"revolve z Bz should be 0 at {name}: {bz}")
        if abs(babs - rev_b) > 3.0:
            fail(f"revolve z |B| at {name}: {babs} want ~{rev_b}")
        hs = 1 if bx > 2 else (-1 if bx < -2 else 0)
        vs = 1 if by > 2 else (-1 if by < -2 else 0)
        z_signs.append((hs, vs))
        if name == "Bh+":
            q0_bh = abs(bx)
        if name == "Bv+":
            q1_bv = abs(by)

        xcmd = revolve_x(ph, amp, False)
        xx, xy, xz, xabs = lab_b_from_coils(*xcmd)
        if abs(xx) > 1.0:
            fail(f"revolve x Bx should be 0 at {name}: {xx}")
        if abs(xabs - rev_b) > 3.0:
            fail(f"revolve x |B| at {name}: {xabs} want ~{rev_b}")
        hs = 1 if xz > 2 else (-1 if xz < -2 else 0)
        vs = 1 if xy > 2 else (-1 if xy < -2 else 0)
        x_signs.append((hs, vs))

    # tilt γ=90: Q0=−X, Q1=+Y, Q2=+X, Q3=−Y
    if z_signs != [(-1, 0), (0, 1), (1, 0), (0, -1)]:
        fail(f"revolve z cardinal order {z_signs} want −Bh,+Bv,+Bh,−Bv (tilt Q0=−X)")
    # tilt γ=0: Q0=+Z, Q1=+Y, Q2=−Z, Q3=−Y
    if x_signs != [(1, 0), (0, 1), (-1, 0), (0, -1)]:
        fail(f"revolve x cardinal order {x_signs} want +Bz,+Bv,−Bz,−Bv")
    if q0_bh is None or q1_bv is None or abs(q0_bh - q1_bv) > 3.0:
        fail(f"Q0 |Bh|={q0_bh} vs Q1 |Bv|={q1_bv} should match without cutting H")

    rev_babs: list[float] = []
    for si in range(0, N, 4):
        ph = (si << 32) // N
        cmd = revolve_z(ph, amp, False)
        _, _, _, babs = lab_b_from_coils(*cmd)
        rev_babs.append(babs)
    if max(rev_babs) - min(rev_babs) > 4.0:
        fail(f"revolve |B| not nearly constant: span {max(rev_babs) - min(rev_babs)}")
    mean_b = sum(rev_babs) / len(rev_babs)
    if abs(mean_b - rev_b) > 3.0:
        fail(f"revolve |B| mean {mean_b} want ~{rev_b}")

    # CCW from Q0 (γ=90, −X) should stay in XY.
    z0 = revolve_z(0, amp, True)
    step = 32 << 22
    z1 = revolve_z(step, amp, True)
    bx0, by0, bz0, _ = lab_b_from_coils(*z0)
    bx1, by1, bz1, _ = lab_b_from_coils(*z1)
    if abs(bz0) > 1.0 or abs(bz1) > 1.0:
        fail("CCW revolve z left the XY plane")
    if abs(bx1 - bx0) < 0.5 and abs(by1 - by0) < 0.5:
        fail("CCW first step did not move the field")

    peak = 200
    if envelope_amp(0, peak) != 0:
        fail("envelope at 0")
    if envelope_amp(peak, peak) != peak:
        fail("envelope at peak")
    mid = envelope_amp(peak // 2, peak)
    want = int(round(peak * math.sin(math.pi / 4)))
    if abs(mid - want) > 2:
        fail(f"envelope mid {mid} want ~{want}")

    # Ah=amp/4, Av=amp: Q0 |Bh|≈Ah·g, Q1 |Bv|≈Av·g
    ah, av = amp // 4, amp
    g = 1.5
    z0 = revolve_z(0, ah, False, av)
    bx, by, _, _ = lab_b_from_coils(*z0)
    if abs(by) > 2.0 or abs(abs(bx) - ah * g) > 3.0:
        fail(f"ellipse Q0 Bx,By={bx},{by} want |Bx|~{ah * g}, By~0")
    z1 = revolve_z(0x40000000, ah, False, av)
    bx, by, _, _ = lab_b_from_coils(*z1)
    if abs(bx) > 2.0 or abs(abs(by) - av * g) > 3.0:
        fail(f"ellipse Q1 Bx,By={bx},{by} want Bx~0, |By|~{av * g}")

    print("ok: rotate Y pairing + lab B (30 deg, |B|~amp*sqrt(3))")
    print("ok: 4-coil revolve X/Z constant |B|~amp*1.5, Q0|Bh|=Q1|Bv|, ellipse")

    check_tilt_mode()
    print("ok: tilted-axis revolve LUT path + reference vectors + Bn=0")

    # Reconstruct: B_i = u_i * coil_axis → forward(u) matches lab_b_from_coils(u)
    axes = (
        (0.0, CV, -CH),
        (-CH, CV, 0.0),
        (0.0, -CV, -CH),
        (-CH, -CV, 0.0),
    )
    raw, _ = tilt_commands_exact(80.0, 80.0, 0.6, 0.8, *tilt_sincos_gamma(30.0))
    bx, by, bz, _ = lab_b_from_coils(*raw)
    uhat = []
    for i, ax in enumerate(axes):
        bi = (raw[i] * ax[0], raw[i] * ax[1], raw[i] * ax[2])
        uhat.append(bi[0] * ax[0] + bi[1] * ax[1] + bi[2] * ax[2])
    rx, ry, rz, _ = lab_b_from_coils(*uhat)
    if abs(rx - bx) > 1e-4 or abs(ry - by) > 1e-4 or abs(rz - bz) > 1e-4:
        fail(f"reconstruct {rx},{ry},{rz} vs {bx},{by},{bz}")
    print("ok: centre reconstruction from coil-axis projections")

    # Behind-coil chip frame (field_tilt_sensor_basis): z = zsign*axis,
    # x = +Y minus its axial part (/c_h), y = z x x. Lab-rotated chip readings
    # must give back u_i on the axis even with transverse junk on x/y.
    zs = (1, -1, -1, 1)
    uhat = []
    for i, ax in enumerate(axes):
        ez = tuple(zs[i] * a for a in ax)
        ex = (-ax[1] * ax[0] / CH, (1.0 - ax[1] * ax[1]) / CH, -ax[1] * ax[2] / CH)
        ey = (ez[1] * ex[2] - ez[2] * ex[1],
              ez[2] * ex[0] - ez[0] * ex[2],
              ez[0] * ex[1] - ez[1] * ex[0])
        if abs(ex[1] - CH) > 1e-9:
            fail(f"M{i + 1} chip x not tilted-up: {ex}")
        bs = (1.5 * (i + 1), -0.7 * i, zs[i] * raw[i])
        lab = tuple(ex[a] * bs[0] + ey[a] * bs[1] + ez[a] * bs[2] for a in range(3))
        uhat.append(lab[0] * ax[0] + lab[1] * ax[1] + lab[2] * ax[2])
    rx, ry, rz, _ = lab_b_from_coils(*uhat)
    if abs(rx - bx) > 1e-4 or abs(ry - by) > 1e-4 or abs(rz - bz) > 1e-4:
        fail(f"chip-frame reconstruct {rx},{ry},{rz} vs {bx},{by},{bz}")
    print("ok: centre reconstruction from behind-coil chip frames")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
