/**
 * Host check of field_tilt.c against the tilted-axis revolve specification.
 *
 *   gcc -O2 -std=c11 -Iinclude -o /tmp/test_field_tilt \
 *       monitor/test_field_tilt.c src/field_tilt.c -lm
 *   /tmp/test_field_tilt
 */
#include "field_tilt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fails;

static void fail(const char *msg)
{
  fprintf(stderr, "FAIL: %s\n", msg);
  g_fails++;
}

static void failf(const char *fmt, float a, float b)
{
  fprintf(stderr, "FAIL: ");
  fprintf(stderr, fmt, (double)a, (double)b);
  fprintf(stderr, "\n");
  g_fails++;
}

static int nearly(float a, float b, float tol)
{
  return fabsf(a - b) <= tol;
}

static void expect(int cond, const char *msg)
{
  if (!cond) fail(msg);
}

static void gamma_of(float deg, float *sg, float *cg)
{
  field_tilt_gamma_sincos(deg, sg, cg);
}

static void check_reference_vectors(void)
{
  float raw[4], bx, by, bz, sg, cg;
  const float A = 100.0f;
  const float tol = 1e-4f;

  gamma_of(0.0f, &sg, &cg);
  field_tilt_commands(A, A, 0.0f, 1.0f, sg, cg, 255.0f, raw, NULL);
  expect(nearly(raw[0], -57.735027f, tol) && nearly(raw[1], 0.0f, tol) &&
             nearly(raw[2], -57.735027f, tol) && nearly(raw[3], 0.0f, tol),
         "gamma=0 theta=0 commands");
  field_tilt_forward(raw, &bx, &by, &bz);
  expect(nearly(bx, 0.0f, tol) && nearly(by, 0.0f, tol) &&
             nearly(bz, 100.0f, tol),
         "gamma=0 theta=0 field");

  gamma_of(45.0f, &sg, &cg);
  field_tilt_commands(A, A, 0.0f, 1.0f, sg, cg, 255.0f, raw, NULL);
  expect(nearly(raw[0], -40.824829f, tol) && nearly(raw[1], 40.824829f, tol) &&
             nearly(raw[2], -40.824829f, tol) && nearly(raw[3], 40.824829f, tol),
         "gamma=45 theta=0 commands");
  field_tilt_forward(raw, &bx, &by, &bz);
  expect(nearly(bx, -70.710678f, tol) && nearly(by, 0.0f, tol) &&
             nearly(bz, 70.710678f, tol),
         "gamma=45 theta=0 field");

  gamma_of(90.0f, &sg, &cg);
  field_tilt_commands(A, A, 0.0f, 1.0f, sg, cg, 255.0f, raw, NULL);
  expect(nearly(raw[0], 0.0f, tol) && nearly(raw[1], 57.735027f, tol) &&
             nearly(raw[2], 0.0f, tol) && nearly(raw[3], 57.735027f, tol),
         "gamma=90 theta=0 commands");
  field_tilt_forward(raw, &bx, &by, &bz);
  expect(nearly(bx, -100.0f, tol) && nearly(by, 0.0f, tol) &&
             nearly(bz, 0.0f, tol),
         "gamma=90 theta=0 field");

  {
    const float gs[] = {0.0f, 15.0f, 30.0f, 45.0f, 60.0f, 75.0f, 90.0f,
                        120.0f, 150.0f, 180.0f};
    int i;
    for (i = 0; i < 10; i++) {
      float c2 = cosf(2.0f * gs[i] * (3.14159265358979323846f / 180.0f));
      float w13 = 0.5f - c2 / 6.0f;
      float w24 = 0.5f + c2 / 6.0f;
      gamma_of(gs[i], &sg, &cg);
      field_tilt_commands(A, A, 1.0f, 0.0f, sg, cg, 255.0f, raw, NULL);
      /* Pure vertical: w13 = 1/2 − cos2γ/6 on M1/M3, w24 = 1/2 + cos2γ/6 on M2/M4. */
      expect(nearly(raw[0], A * w13, tol) && nearly(raw[1], A * w24, tol) &&
                 nearly(raw[2], -A * w13, tol) && nearly(raw[3], -A * w24, tol),
             "theta=90 vertical split w13/w24");
      field_tilt_forward(raw, &bx, &by, &bz);
      expect(nearly(bx, 0.0f, tol) && nearly(by, 100.0f, tol) &&
                 nearly(bz, 0.0f, tol),
             "any gamma theta=90 field");
    }
  }
}

static void sweep_math(void)
{
  const float gs[] = {0.0f, 15.0f, 30.0f, 45.0f, 60.0f, 75.0f, 90.0f,
                      120.0f, 150.0f, 180.0f};
  const float A = 100.0f;
  const float tol = 2e-4f;
  int gi, si;

  for (gi = 0; gi < 10; gi++) {
    float sg, cg;
    gamma_of(gs[gi], &sg, &cg);
    for (si = 0; si < 360; si++) {
      float th = (float)si * (3.14159265358979323846f / 180.0f);
      float sth = sinf(th);
      float cth = cosf(th);
      float H = A * cth;
      float V = A * sth;
      float raw[4], bx, by, bz, bh, bv, bn, mag;
      float want_x = -H * sg;
      float want_y = V;
      float want_z = H * cg;

      field_tilt_commands(A, A, sth, cth, sg, cg, 255.0f, raw, NULL);
      field_tilt_forward(raw, &bx, &by, &bz);
      if (!nearly(bx, want_x, tol) || !nearly(by, want_y, tol) ||
          !nearly(bz, want_z, tol)) {
        failf("forward != target at gamma sweep (bx %g vs %g)", bx, want_x);
        return;
      }
      bh = -bx * sg + bz * cg;
      bv = by;
      bn = bx * cg + bz * sg;
      if (!nearly(bn, 0.0f, tol)) {
        failf("Bn != 0 (%g vs %g)", bn, 0.0f);
        return;
      }
      if (!nearly(bh, H, tol) || !nearly(bv, V, tol)) {
        failf("Bh/Bv mismatch (%g vs %g)", bh, H);
        return;
      }
      mag = sqrtf(bx * bx + by * by + bz * bz);
      if (!nearly(mag, A, tol)) {
        failf("|B| != A (%g vs %g)", mag, A);
        return;
      }
    }
  }
}

static void sweep_ellipse(void)
{
  float sg, cg, raw[4], bx, by, bz;
  const float Ah = 40.0f;
  const float Av = 100.0f;
  int si;

  gamma_of(30.0f, &sg, &cg);
  for (si = 0; si < 360; si++) {
    float th = (float)si * (3.14159265358979323846f / 180.0f);
    float sth = sinf(th);
    float cth = cosf(th);
    float mag2, want2;
    field_tilt_commands(Ah, Av, sth, cth, sg, cg, 255.0f, raw, NULL);
    field_tilt_forward(raw, &bx, &by, &bz);
    mag2 = bx * bx + by * by + bz * bz;
    want2 = Ah * Ah * cth * cth + Av * Av * sth * sth;
    if (!nearly(mag2, want2, 0.05f)) {
      failf("ellipse |B|^2 (%g vs %g)", mag2, want2);
      return;
    }
  }
}

static void check_zero_amp(void)
{
  const float gs[] = {0.0f, 45.0f, 90.0f};
  int i, k;
  for (i = 0; i < 3; i++) {
    float sg, cg, raw[4];
    gamma_of(gs[i], &sg, &cg);
    field_tilt_commands(0.0f, 0.0f, 0.7f, 0.7f, sg, cg, 255.0f, raw, NULL);
    for (k = 0; k < 4; k++) {
      if (raw[k] != 0.0f) {
        fail("zero amplitude must produce zero commands");
        return;
      }
    }
  }
}

static void check_reverse_phase(void)
{
  float sg, cg, fwd[4], rev[4], bx, by, bz, rx, ry, rz;
  const float A = 80.0f;
  float th = 40.0f * (3.14159265358979323846f / 180.0f);
  float mag, rmag;

  gamma_of(60.0f, &sg, &cg);
  field_tilt_commands(A, A, sinf(th), cosf(th), sg, cg, 255.0f, fwd, NULL);
  field_tilt_commands(A, A, sinf(-th), cosf(-th), sg, cg, 255.0f, rev, NULL);
  field_tilt_forward(fwd, &bx, &by, &bz);
  field_tilt_forward(rev, &rx, &ry, &rz);
  mag = sqrtf(bx * bx + by * by + bz * bz);
  rmag = sqrtf(rx * rx + ry * ry + rz * rz);
  expect(nearly(mag, rmag, 1e-4f), "reverse phase keeps |B|");
  expect(nearly(bx * cg + bz * sg, 0.0f, 1e-4f) &&
             nearly(rx * cg + rz * sg, 0.0f, 1e-4f),
         "reverse phase stays in plane (Bn=0)");
  expect(!nearly(bx, rx, 1e-3f) || !nearly(by, ry, 1e-3f),
         "reverse phase must change the in-plane sample");
}

static void check_quantize_signed(void)
{
  float raw[4] = {-70.710678f, 0.4f, -0.5f, 50.0f};
  int16_t cmd[4];
  field_tilt_quantize(raw, cmd, 255);
  expect(cmd[0] == -71, "negative round-away from 0: -70.71 -> -71");
  expect(cmd[1] == 0, "0.4 -> 0");
  expect(cmd[2] == -1, "-0.5 -> -1 (ties away from 0)");
  expect(cmd[3] == 50, "50.0 stays 50");

  raw[0] = 300.0f;
  raw[1] = -300.0f;
  field_tilt_quantize(raw, cmd, 255);
  expect(cmd[0] == 255 && cmd[1] == -255, "quantize clamp ±255");
}

static void check_shared_scale_limits(void)
{
  float sg, cg, raw[4], scale;
  int16_t cmd[4];
  int i;

  gamma_of(0.0f, &sg, &cg);
  field_tilt_commands(510.0f, 510.0f, 0.0f, 1.0f, sg, cg, 255.0f, raw, &scale);
  expect(scale < 1.0f, "over-range radii must share-scale");
  {
    float peak = 0.0f;
    int si;
    for (si = 0; si < 360; si++) {
      float th = (float)si * (3.14159265358979323846f / 180.0f);
      field_tilt_commands(510.0f, 510.0f, sinf(th), cosf(th), sg, cg,
                          255.0f, raw, NULL);
      field_tilt_quantize(raw, cmd, 255);
      for (i = 0; i < 4; i++) {
        float a = fabsf(raw[i]);
        if (a > peak) peak = a;
        if (cmd[i] > 255 || cmd[i] < -255) {
          fail("scaled commands still out of range");
          return;
        }
      }
    }
    expect(nearly(peak, 255.0f, 0.05f), "shared scale should sit on Umax over a revolution");
  }
}

static void check_no_independent_clip_direction(void)
{
  float sg, cg, raw[4], bx, by, bz, scale;
  const float Ah = 400.0f;
  const float Av = 400.0f;
  float th = 20.0f * (3.14159265358979323846f / 180.0f);
  float H, V;

  gamma_of(45.0f, &sg, &cg);
  field_tilt_commands(Ah, Av, sinf(th), cosf(th), sg, cg, 255.0f, raw, &scale);
  H = (Ah * scale) * cosf(th);
  V = (Av * scale) * sinf(th);
  field_tilt_forward(raw, &bx, &by, &bz);
  expect(nearly(bx, -H * sg, 1e-3f) && nearly(by, V, 1e-3f) &&
             nearly(bz, H * cg, 1e-3f),
         "shared scale preserves field direction");
}

static void check_max_circle(void)
{
  const float gs[] = {0.0f, 20.0f, 45.0f, 70.0f, 90.0f, 135.0f, 180.0f};
  int gi, si, k;

  for (gi = 0; gi < 7; gi++) {
    float sg, cg, R, mn = 1e9f, mx = 0.0f, scale, c2;
    float peak[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    gamma_of(gs[gi], &sg, &cg);
    c2 = cosf(2.0f * gs[gi] * (3.14159265358979323846f / 180.0f));
    expect(nearly(field_tilt_max_gain(sg, cg), 6.0f / sqrtf(15.0f + c2 * c2), 1e-5f),
           "max gain = 6 / sqrt(15 + cos²2γ)");
    R = 255.0f * field_tilt_max_gain(sg, cg);
    for (si = 0; si < 720; si++) {
      float th = (float)si * (3.14159265358979323846f / 360.0f);
      float raw[4], bx, by, bz, m;
      field_tilt_commands(R, R, sinf(th), cosf(th), sg, cg, 255.0f, raw, &scale);
      if (!nearly(scale, 1.0f, 1e-4f)) {
        fail("max circle should not need shared scale");
        return;
      }
      for (k = 0; k < 4; k++) if (fabsf(raw[k]) > peak[k]) peak[k] = fabsf(raw[k]);
      field_tilt_forward(raw, &bx, &by, &bz);
      m = sqrtf(bx * bx + by * by + bz * bz);
      if (m < mn) mn = m;
      if (m > mx) mx = m;
    }
    for (k = 0; k < 4; k++) {
      expect(nearly(peak[k], 255.0f, 0.05f), "max circle: every coil peaks at 255");
    }
    expect(nearly(mn, R, 0.05f) && nearly(mx, R, 0.05f), "max circle |B| constant");
    expect(field_tilt_shared_scale(R * 1.01f, R * 1.01f, sg, cg, 255.0f) < 1.0f,
           "circle above R_max must be scaled");
  }
  expect(nearly(field_tilt_max_gain(0.0f, 1.0f), 1.5f, 1e-5f), "revolve X gain 1.5");
  expect(nearly(field_tilt_max_gain(1.0f, 0.0f), 1.5f, 1e-5f), "revolve Z gain 1.5");

  /* Revolve X roles: M1/M3 = H + V/3, M2/M4 = 2V/3, no H on M2/M4. */
  {
    float sg, cg, raw[4];
    const float R = 300.0f;
    gamma_of(0.0f, &sg, &cg);
    for (si = 0; si < 360; si += 7) {
      float th = (float)si * (3.14159265358979323846f / 180.0f);
      float H = R * cosf(th), V = R * sinf(th);
      field_tilt_commands(R, R, sinf(th), cosf(th), sg, cg, 255.0f, raw, NULL);
      if (!nearly(raw[0], -H / 1.7320508f + V / 3.0f, 1e-2f) ||
          !nearly(raw[1], 2.0f * V / 3.0f, 1e-2f) ||
          !nearly(raw[2], -H / 1.7320508f - V / 3.0f, 1e-2f) ||
          !nearly(raw[3], -2.0f * V / 3.0f, 1e-2f)) {
        fail("revolve X: M1/M3 = H + V/3, M2/M4 = 2V/3");
        return;
      }
    }
  }
}

static void check_parse(void)
{
  float v = 123.0f;
  expect(field_tilt_parse_gamma("0", &v) && v == 0.0f, "parse 0");
  expect(field_tilt_parse_gamma("90", &v) && v == 90.0f, "parse 90");
  expect(field_tilt_parse_gamma("45.0", &v) && nearly(v, 45.0f, 1e-5f), "parse 45.0");
  expect(field_tilt_parse_gamma("15.5", &v) && nearly(v, 15.5f, 1e-5f), "parse 15.5");
  expect(field_tilt_parse_gamma("135", &v) && v == 135.0f, "parse 135");
  expect(field_tilt_parse_gamma("180", &v) && v == 180.0f, "parse 180");
  v = 7.0f;
  expect(!field_tilt_parse_gamma("181", &v) && v == 7.0f, "reject 181, no write");
  expect(!field_tilt_parse_gamma("-1", &v) && v == 7.0f, "reject -1, no write");
  expect(!field_tilt_parse_gamma("45x", &v) && v == 7.0f, "reject 45x");
  expect(!field_tilt_parse_gamma("360", &v) && v == 7.0f, "reject wrap 360");
  expect(!field_tilt_parse_gamma("", &v) && v == 7.0f, "reject empty");
  expect(!field_tilt_parse_gamma(NULL, &v) && v == 7.0f, "reject NULL");
  expect(!field_tilt_parse_gamma("abc", &v) && v == 7.0f, "reject abc");
}

static void check_endpoint_planes(void)
{
  float sg, cg, raw[4], bx, by, bz;
  const float A = 100.0f;
  float th = 33.0f * (3.14159265358979323846f / 180.0f);
  float H = A * cosf(th);
  float V = A * sinf(th);

  gamma_of(0.0f, &sg, &cg);
  field_tilt_commands(A, A, sinf(th), cosf(th), sg, cg, 255.0f, raw, NULL);
  field_tilt_forward(raw, &bx, &by, &bz);
  expect(nearly(bx, 0.0f, 1e-4f) && nearly(by, V, 1e-4f) && nearly(bz, H, 1e-4f),
         "gamma=0 plane is YZ (0, V, H)");

  gamma_of(90.0f, &sg, &cg);
  field_tilt_commands(A, A, sinf(th), cosf(th), sg, cg, 255.0f, raw, NULL);
  field_tilt_forward(raw, &bx, &by, &bz);
  expect(nearly(bx, -H, 1e-4f) && nearly(by, V, 1e-4f) && nearly(bz, 0.0f, 1e-4f),
         "gamma=90 plane is XY (−H, V, 0)");
}

static void check_coherent_coeffs(void)
{
  FieldTiltCoeffs a, b, live;

  memset(&a, 0x5a, sizeof(a));
  field_tilt_coeffs_from_deg(30.0f, &a);
  field_tilt_coeffs_from_deg(60.0f, &b);
  live = a;
  live = b; /* whole-struct assign: sin and cos cannot tear */
  expect(live.deg == b.deg && live.sin_g == b.sin_g && live.cos_g == b.cos_g,
         "coeff struct assign is coherent");
  expect(nearly(live.sin_g, sinf(60.0f * 3.14159265358979323846f / 180.0f), 1e-5f) &&
             nearly(live.cos_g, cosf(60.0f * 3.14159265358979323846f / 180.0f), 1e-5f),
         "60 deg sincos");
  expect(a.sin_g != b.sin_g && a.cos_g != b.cos_g,
         "30 vs 60 coeffs must both change");
}

static void check_quantized_field_bound(void)
{
  float sg, cg, raw[4], bx, by, bz;
  int16_t cmd[4];
  float u[4];
  const float A = 100.0f;
  const float ch = FIELD_CH;
  const float cv = FIELD_CV;
  int si;

  gamma_of(45.0f, &sg, &cg);
  for (si = 0; si < 360; si += 3) {
    float th = (float)si * (3.14159265358979323846f / 180.0f);
    int k;
    field_tilt_commands(A, A, sinf(th), cosf(th), sg, cg, 255.0f, raw, NULL);
    field_tilt_quantize(raw, cmd, 255);
    for (k = 0; k < 4; k++) {
      expect(cmd[k] <= 255 && cmd[k] >= -255, "quantized within limits");
      u[k] = (float)cmd[k];
      if (fabsf(u[k] - raw[k]) > 0.5f + 1e-4f) {
        fail("per-coil rounding error > 0.5");
        return;
      }
    }
    field_tilt_forward(u, &bx, &by, &bz);
    {
      float H = A * cosf(th);
      float V = A * sinf(th);
      float ex = fabsf(bx - (-H * sg));
      float ey = fabsf(by - V);
      float ez = fabsf(bz - (H * cg));
      if (ex > ch + 1e-3f || ez > ch + 1e-3f || ey > 2.0f * cv + 1e-3f) {
        fail("quantized forward error exceeds c_h / 2 c_v bound");
        return;
      }
    }
  }
}

static void check_reconstruct(void)
{
  float sg, cg, raw[4], bx, by, bz, rx, ry, rz;
  float b_lab[4][3];
  float k[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  uint8_t present[4] = {1, 1, 1, 1};
  const float A = 80.0f;
  int i;

  gamma_of(30.0f, &sg, &cg);
  field_tilt_commands(A, A, 0.6f, 0.8f, sg, cg, 255.0f, raw, NULL);
  field_tilt_forward(raw, &bx, &by, &bz);
  for (i = 0; i < 4; i++) {
    float ax[3];
    field_tilt_coil_axis((uint8_t)i, ax);
    b_lab[i][0] = raw[i] * ax[0];
    b_lab[i][1] = raw[i] * ax[1];
    b_lab[i][2] = raw[i] * ax[2];
  }
  field_tilt_reconstruct_center(b_lab, k, present, &rx, &ry, &rz);
  expect(nearly(rx, bx, 1e-4f) && nearly(ry, by, 1e-4f) && nearly(rz, bz, 1e-4f),
         "reconstruct from coil-axis projections matches forward");
}

static float dot3(const float a[3], const float b[3])
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void check_sensor_frame(void)
{
  float sg, cg, raw[4], bx, by, bz, rx, ry, rz;
  float b_s[4][3];
  const int8_t zs[4] = {1, -1, -1, 1};
  const float k[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const uint8_t present[4] = {1, 1, 1, 1};
  int i, s;

  /* Right-handed orthonormal, z = ±â, x leans toward lab +Y. */
  for (i = 0; i < 4; i++) {
    for (s = -1; s <= 1; s += 2) {
      float ex[3], ey[3], ez[3], ax[3], c[3];
      field_tilt_sensor_basis((uint8_t)i, (int8_t)s, ex, ey, ez);
      field_tilt_coil_axis((uint8_t)i, ax);
      c[0] = ex[1] * ey[2] - ex[2] * ey[1];
      c[1] = ex[2] * ey[0] - ex[0] * ey[2];
      c[2] = ex[0] * ey[1] - ex[1] * ey[0];
      expect(nearly(dot3(ex, ex), 1.0f, 1e-5f) && nearly(dot3(ey, ey), 1.0f, 1e-5f) &&
                 nearly(dot3(ez, ez), 1.0f, 1e-5f) && nearly(dot3(ex, ey), 0.0f, 1e-5f) &&
                 nearly(dot3(ex, ez), 0.0f, 1e-5f) && nearly(dot3(ey, ez), 0.0f, 1e-5f),
             "sensor basis orthonormal");
      expect(nearly(c[0], ez[0], 1e-5f) && nearly(c[1], ez[1], 1e-5f) &&
                 nearly(c[2], ez[2], 1e-5f),
             "sensor basis right-handed (x × y = z)");
      expect(nearly(dot3(ez, ax), (float)s, 1e-5f), "sensor z = z_sign · coil axis");
      expect(nearly(ex[1], FIELD_CH, 1e-5f), "sensor x is up, tilted 30°");
    }
  }

  /* Each chip reads its coil's centre field on +z (times z_sign) plus
   * transverse junk; the centre must match the forward map regardless. */
  gamma_of(60.0f, &sg, &cg);
  field_tilt_commands(90.0f, 70.0f, -0.28f, 0.96f, sg, cg, 255.0f, raw, NULL);
  field_tilt_forward(raw, &bx, &by, &bz);
  for (i = 0; i < 4; i++) {
    b_s[i][0] = 3.0f * (float)(i + 1);
    b_s[i][1] = -2.0f * (float)i;
    b_s[i][2] = (float)zs[i] * raw[i];
  }
  field_tilt_center_from_sensors(b_s, zs, k, present, &rx, &ry, &rz);
  expect(nearly(rx, bx, 1e-3f) && nearly(ry, by, 1e-3f) && nearly(rz, bz, 1e-3f),
         "centre from chip frames matches forward (transverse ignored)");

  /* Lab rotation round-trips the axial reading onto â_i. */
  {
    float lab[3], ax[3];
    field_tilt_sensor_to_lab(2, -1, b_s[2], lab);
    field_tilt_coil_axis(2, ax);
    expect(nearly(dot3(lab, ax), raw[2], 1e-3f), "sensor_to_lab axial projection");
  }

  /* Missing chip contributes nothing. */
  {
    uint8_t p[4] = {1, 0, 1, 1};
    float u[4];
    u[0] = raw[0]; u[1] = 0.0f; u[2] = raw[2]; u[3] = raw[3];
    field_tilt_forward(u, &bx, &by, &bz);
    field_tilt_center_from_sensors(b_s, zs, k, p, &rx, &ry, &rz);
    expect(nearly(rx, bx, 1e-3f) && nearly(ry, by, 1e-3f) && nearly(rz, bz, 1e-3f),
           "centre from chip frames with M2 missing");
  }
}

int main(void)
{
  check_parse();
  check_reference_vectors();
  sweep_math();
  sweep_ellipse();
  check_zero_amp();
  check_reverse_phase();
  check_quantize_signed();
  check_shared_scale_limits();
  check_no_independent_clip_direction();
  check_endpoint_planes();
  check_coherent_coeffs();
  check_quantized_field_bound();
  check_reconstruct();
  check_sensor_frame();
  check_max_circle();

  if (g_fails) {
    fprintf(stderr, "%d check(s) failed\n", g_fails);
    return 1;
  }
  printf("ok: field_tilt helper reference vectors, Bn=0, |B|, ellipse, parse, scale\n");
  return 0;
}
