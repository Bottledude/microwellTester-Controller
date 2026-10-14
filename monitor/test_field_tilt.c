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
    const float gs[] = {0.0f, 15.0f, 30.0f, 45.0f, 60.0f, 75.0f, 90.0f};
    int i;
    for (i = 0; i < 7; i++) {
      gamma_of(gs[i], &sg, &cg);
      field_tilt_commands(A, A, 1.0f, 0.0f, sg, cg, 255.0f, raw, NULL);
      expect(nearly(raw[0], 50.0f, tol) && nearly(raw[1], 50.0f, tol) &&
                 nearly(raw[2], -50.0f, tol) && nearly(raw[3], -50.0f, tol),
             "any gamma theta=90 commands");
      field_tilt_forward(raw, &bx, &by, &bz);
      expect(nearly(bx, 0.0f, tol) && nearly(by, 100.0f, tol) &&
                 nearly(bz, 0.0f, tol),
             "any gamma theta=90 field");
    }
  }
}

static void sweep_math(void)
{
  const float gs[] = {0.0f, 15.0f, 30.0f, 45.0f, 60.0f, 75.0f, 90.0f};
  const float A = 100.0f;
  const float tol = 2e-4f;
  int gi, si;

  for (gi = 0; gi < 7; gi++) {
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

static void check_parse(void)
{
  float v = 123.0f;
  expect(field_tilt_parse_gamma("0", &v) && v == 0.0f, "parse 0");
  expect(field_tilt_parse_gamma("90", &v) && v == 90.0f, "parse 90");
  expect(field_tilt_parse_gamma("45.0", &v) && nearly(v, 45.0f, 1e-5f), "parse 45.0");
  expect(field_tilt_parse_gamma("15.5", &v) && nearly(v, 15.5f, 1e-5f), "parse 15.5");
  v = 7.0f;
  expect(!field_tilt_parse_gamma("91", &v) && v == 7.0f, "reject 91, no write");
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

  if (g_fails) {
    fprintf(stderr, "%d check(s) failed\n", g_fails);
    return 1;
  }
  printf("ok: field_tilt helper reference vectors, Bn=0, |B|, ellipse, parse, scale\n");
  return 0;
}
