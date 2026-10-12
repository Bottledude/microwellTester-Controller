#!/usr/bin/env python3
"""Host check of rotate-Y / constant-|B| revolve-X/Z coil currents vs the lab-frame map."""
from __future__ import annotations

import math
import sys

SQRT2_2 = math.sqrt(2.0) / 2.0
N = 360
LUT_45 = 45
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


def sine_scaled_exact(idx: int, amp: int) -> int:
    """Match firmware sine_scaled_exact() — ±amp at LUT sin = ±1, symmetric ±."""
    idx = lut_mod(idx)
    if idx == LUT_90:
        return amp
    if idx == LUT_90 + 180:
        return -amp
    q15 = int(round(math.sin(2.0 * math.pi * idx / N) * 32767.0))
    if q15 > 32767:
        q15 = 32767
    if q15 < -32767:
        q15 = -32767
    if q15 >= 0:
        return (q15 * amp) >> 15
    mag = ((-q15) * amp) >> 15
    return -mag


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


def revolve_pair(phase: int, amp_h: int, ccw: bool = False, amp_v: int | None = None) -> tuple[int, int]:
    """Match firmware revolve_pair_currents() (equal Ah=Av uses integer path)."""
    if amp_v is None:
        amp_v = amp_h
    ph = phase & 0xFFFFFFFF
    if ccw:
        ph = (-ph) & 0xFFFFFFFF
    si = lut_from_phase(ph)
    if amp_h == amp_v:
        i1 = sine_scaled_exact(si - LUT_45, amp_h)
        i2 = -sine_scaled_exact(si + LUT_45, amp_h)
        if i1 > 255:
            i1 = 255
        if i1 < -255:
            i1 = -255
        if i2 > 255:
            i2 = 255
        if i2 < -255:
            i2 = -255
        return i1, i2
    s = lut_unit(si)
    c = lut_unit(si + LUT_90)
    i1 = clamp_pwm(SQRT2_2 * (float(amp_v) * s - float(amp_h) * c))
    i2 = clamp_pwm(-SQRT2_2 * (float(amp_h) * c + float(amp_v) * s))
    return i1, i2


def revolve_z(phase: int, amp: int, ccw: bool) -> tuple[int, int, int, int]:
    i1, i2 = revolve_pair(phase, amp, ccw)
    return 0, i1, 0, i2


def revolve_x(phase: int, amp: int, ccw: bool) -> tuple[int, int, int, int]:
    i1, i2 = revolve_pair(phase, amp, ccw)
    return i1, 0, i2, 0


def lab_from_pair(b1: float, b2: float) -> tuple[float, float]:
    """Coil-to-lab map used for both XY (revolve z) and YZ (revolve x)."""
    horiz = -SQRT2_2 * (b1 + b2)
    vert = SQRT2_2 * (b1 - b2)
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


def main() -> int:
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
        want_bx = -math.sqrt(2.0) * float(m2)
        want_bz = -math.sqrt(2.0) * float(m1)
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

    # Cardinals: ±amp/√2, lab order +Bh, +Bv, −Bh, −Bv. |B| ≈ amp.
    r = int(round(amp * SQRT2_2))
    corners = [
        (0x00000000, -r, -r, "Bh+"),
        (0x40000000, r, -r, "Bv+"),
        (0x80000000, r, r, "Bh-"),
        (0xC0000000, -r, r, "Bv-"),
    ]
    signs = []
    for ph, e1, e2, name in corners:
        i1, i2 = revolve_pair(ph, amp, False)
        if abs(i1 - e1) > 2 or abs(i2 - e2) > 2:
            fail(f"corner {name} currents {i1},{i2} want {e1},{e2}")
        bh, bv = lab_from_pair(float(i1), float(i2))
        hs = 1 if bh > 0 else (-1 if bh < 0 else 0)
        vs = 1 if bv > 0 else (-1 if bv < 0 else 0)
        signs.append((hs, vs))
        if abs(math.hypot(bh, bv) - amp) > 3.0:
            fail(f"corner {name} |B|={math.hypot(bh, bv)} want ~{amp}")
        m1, m2, m3, m4 = revolve_z(ph, amp, False)
        if m1 != 0 or m3 != 0:
            fail(f"revolve z left M1/M3 on at {name}")
        if abs(m2 - i1) > 0 or abs(m4 - i2) > 0:
            fail(f"revolve z map at {name}")
        bx, by, bz, babs = lab_b_from_coils(m1, m2, m3, m4)
        if abs(bz) > 1e-6:
            fail(f"revolve z Bz should be 0 at {name}: {bz}")
        if abs(bx - bh) > 1e-4 or abs(by - bv) > 1e-4:
            fail(f"revolve z Bx,By at {name}: {bx},{by} want {bh},{bv}")
        if abs(babs - math.hypot(bh, bv)) > 1e-4:
            fail(f"revolve z |B| at {name}: {babs}")
        x1, x2, x3, x4 = revolve_x(ph, amp, False)
        if x2 != 0 or x4 != 0:
            fail(f"revolve x left M2/M4 on at {name}")
        if abs(x1 - i1) > 0 or abs(x3 - i2) > 0:
            fail(f"revolve x map at {name}")
        xx, xy, xz, xabs = lab_b_from_coils(x1, x2, x3, x4)
        if abs(xx) > 1e-6:
            fail(f"revolve x Bx should be 0 at {name}: {xx}")
        if abs(xz - bh) > 1e-4 or abs(xy - bv) > 1e-4:
            fail(f"revolve x Bz,By at {name}: {xz},{xy} want {bh},{bv}")
        if abs(xabs - math.hypot(bh, bv)) > 1e-4:
            fail(f"revolve x |B| at {name}: {xabs}")

    if signs != [(1, 0), (0, 1), (-1, 0), (0, -1)]:
        fail(f"lab cardinal order {signs} want +Bh,+Bv,-Bh,-Bv")

    # Q0.5 (CW): one coil 0, the other −amp. amp=255 hits exactly.
    i1m, i2m = revolve_pair(0x20000000, 255, False)
    if i1m != 0 or i2m != -255:
        fail(f"Q0.5 amp=255 currents {i1m},{i2m} want 0,-255")
    i1m, i2m = revolve_pair(0x20000000, amp, False)
    if abs(i1m) > 1 or abs(i2m + amp) > 1:
        fail(f"Q0.5 currents {i1m},{i2m} want 0,{-amp}")

    rev_babs: list[float] = []
    for si in range(0, N, 4):
        ph = (si << 32) // N
        i1, i2 = revolve_pair(ph, amp, False)
        bh, bv = lab_from_pair(float(i1), float(i2))
        rev_babs.append(math.hypot(bh, bv))
    if max(rev_babs) - min(rev_babs) > 4.0:
        fail(f"revolve |B| not nearly constant: span {max(rev_babs) - min(rev_babs)}")
    mean_b = sum(rev_babs) / len(rev_babs)
    if abs(mean_b - amp) > 3.0:
        fail(f"revolve |B| mean {mean_b} want ~{amp}")

    # CCW from +Bh goes toward −Bv: I1 more negative, I2 toward 0.
    i1s, i2s = revolve_pair(0, amp, True)
    if abs(i1s - (-r)) > 2 or abs(i2s - (-r)) > 2:
        fail(f"CCW start not +Bh corner: {i1s},{i2s} want {-r},{-r}")
    step = 32 << 22  # 1/8 of a quadrant
    i1n, i2n = revolve_pair(step, amp, True)
    if i1n >= i1s:
        fail(f"CCW first step I1 should go toward −amp: {i1s} -> {i1n}")
    if i2n <= i2s:
        fail(f"CCW first step I2 should go toward 0: {i2s} -> {i2n}")

    # Envelope: 0 at live=0, peak at live=peak, concave like sin(π/2 p).
    peak = 200
    if envelope_amp(0, peak) != 0:
        fail("envelope at 0")
    if envelope_amp(peak, peak) != peak:
        fail("envelope at peak")
    mid = envelope_amp(peak // 2, peak)
    want = int(round(peak * math.sin(math.pi / 4)))
    if abs(mid - want) > 2:
        fail(f"envelope mid {mid} want ~{want}")

    # Parked-corner phases (CW stored phase = Q << 30) match the four lab states.
    park = ((0, -r, -r), (1, r, -r), (2, r, r), (3, -r, r))
    for q, e1, e2 in park:
        i1, i2 = revolve_pair(q << 30, amp, False)
        if abs(i1 - e1) > 2 or abs(i2 - e2) > 2:
            fail(f"park Q{q} {i1},{i2} want {e1},{e2}")
        ph_ccw = (-(q << 30)) & 0xFFFFFFFF
        j1, j2 = revolve_pair(ph_ccw, amp, True)
        if abs(j1 - e1) > 2 or abs(j2 - e2) > 2:
            fail(f"park Q{q} CCW-encoded {j1},{j2} want {e1},{e2}")

    # Ah=amp/4, Av=amp: Q0 |Bh|≈Ah, Q1 |Bv|≈Av (H cut 4× vs equal circle).
    ah, av = amp // 4, amp
    i1, i2 = revolve_pair(0, ah, False, av)
    bh, bv = lab_from_pair(float(i1), float(i2))
    if abs(bv) > 2.0 or abs(abs(bh) - ah) > 3.0:
        fail(f"ellipse Q0 Bh,Bv={bh},{bv} want |Bh|~{ah}, Bv~0")
    i1, i2 = revolve_pair(0x40000000, ah, False, av)
    bh, bv = lab_from_pair(float(i1), float(i2))
    if abs(bh) > 2.0 or abs(abs(bv) - av) > 3.0:
        fail(f"ellipse Q1 Bh,Bv={bh},{bv} want Bh~0, |Bv|~{av}")
    if abs(ah * 4 - av) > 1:
        fail("test amp not 4:1")

    print("ok: rotate Y pairing + lab B, constant-|B| revolve corners + Q0.5 + CCW")
    print("ok: rev_hv Ah/Av ellipse (H 4x smaller at Q0 vs Q1)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
