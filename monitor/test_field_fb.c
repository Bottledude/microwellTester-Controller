/**
 * Host check of field_fb.c against a simulated rig: four coils with unequal
 * strength and phase lag, a few % crosstalk, chip offsets, noise and a delay
 * common to all chips.
 *
 *   gcc -O2 -std=c11 -Wall -Wextra -Iinclude -o /tmp/test_field_fb \
 *       monitor/test_field_fb.c src/field_fb.c src/field_tilt.c -lm
 *   /tmp/test_field_fb
 */
#include "field_fb.h"
#include "field_tilt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI_F 3.14159265358979323846f

static int g_fails;

static void expect(int cond, const char *msg)
{
  if (!cond) {
    fprintf(stderr, "FAIL: %s\n", msg);
    g_fails++;
  }
}

static FbCplx cx(float re, float im)
{
  FbCplx z;
  z.re = re;
  z.im = im;
  return z;
}

static FbCplx polar(float mag, float deg)
{
  float r = deg * (PI_F / 180.0f);
  return cx(mag * cosf(r), mag * sinf(r));
}

static FbCplx cmul(FbCplx a, FbCplx b)
{
  return cx(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}

static unsigned s_rng = 12345u;
static float noise(float amp)
{
  s_rng = s_rng * 1103515245u + 12345u;
  return amp * (((float)((s_rng >> 8) & 0xFFFFu) / 32767.5f) - 1.0f);
}

typedef struct {
  FbCplx c[4][4]; /* chip i axial response to coil j (mT per duty) */
  float  offset[4];
  float  noise;
} Plant;

static void plant_default(Plant *p, int flip_coil)
{
  const float mag[4] = {1.00f, 0.78f, 1.18f, 0.90f};
  const float lag[4] = {-6.0f, -22.0f, -11.0f, -31.0f};
  const float common = -15.0f; /* sensor conversion delay, same on all chips */
  int i, j;
  memset(p, 0, sizeof(*p));
  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      float m = (i == j) ? mag[i] : 0.03f;
      float d = (i == j) ? lag[i] : (lag[j] - 40.0f);
      p->c[i][j] = polar(0.02f * m, d + common);
    }
    p->offset[i] = 0.04f * (float)(i + 1);
  }
  if (flip_coil >= 0) {
    p->c[flip_coil][flip_coil].re = -p->c[flip_coil][flip_coil].re;
    p->c[flip_coil][flip_coil].im = -p->c[flip_coil][flip_coil].im;
  }
  p->noise = 0.002f;
}

static void ideal_coeffs(float gamma_deg, float amp, float alpha[4], float beta[4])
{
  float sg, cg, R;
  field_tilt_gamma_sincos(gamma_deg, &sg, &cg);
  R = amp * field_tilt_max_gain(sg, cg);
  field_tilt_commands(R, R, 0.0f, 1.0f, sg, cg, 255.0f, alpha, NULL);
  field_tilt_commands(R, R, 1.0f, 0.0f, sg, cg, 255.0f, beta, NULL);
}

/* Run one window of n samples at f Hz (50 Hz chip rate) starting at sample k0. */
static void run_window(FieldFb *fb, const Plant *p, const float alpha[4],
                       const float beta[4], float f, int k0, int n, float *peak)
{
  int k, i, j;
  for (k = k0; k < k0 + n; k++) {
    float th = 2.0f * PI_F * f * (float)k / 50.0f;
    float c = cosf(th), s = sinf(th);
    float m[4];
    for (j = 0; j < 4; j++) {
      float u = field_fb_coil(fb->k[j], alpha[j], beta[j], c, s);
      if (peak && fabsf(u) > *peak) *peak = fabsf(u);
    }
    for (i = 0; i < 4; i++) {
      FbCplx P = cx(0.0f, 0.0f);
      for (j = 0; j < 4; j++) {
        FbCplx D = cmul(fb->k[j], cx(alpha[j], -beta[j]));
        FbCplx t = cmul(p->c[i][j], D);
        P.re += t.re;
        P.im += t.im;
      }
      m[i] = P.re * c - P.im * s + p->offset[i] + noise(p->noise);
    }
    field_fb_add(fb, th, m);
  }
}

/* |B| ripple of the true centre field (Σ_i axial_i â_i) over one revolution. */
static float true_ripple(const FieldFb *fb, const Plant *p, const float alpha[4],
                         const float beta[4])
{
  float mn = 1e9f, mx = 0.0f;
  int k, i, j;
  for (k = 0; k < 360; k++) {
    float th = (float)k * (PI_F / 180.0f);
    float c = cosf(th), s = sinf(th), m[4], b[3], mag;
    for (i = 0; i < 4; i++) {
      FbCplx P = cx(0.0f, 0.0f);
      for (j = 0; j < 4; j++) {
        FbCplx t = cmul(p->c[i][j], cmul(fb->k[j], cx(alpha[j], -beta[j])));
        P.re += t.re;
        P.im += t.im;
      }
      m[i] = P.re * c - P.im * s;
    }
    field_tilt_forward(m, &b[0], &b[1], &b[2]);
    mag = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    if (mag < mn) mn = mag;
    if (mag > mx) mx = mag;
  }
  return (mx - mn) / (0.5f * (mx + mn));
}

static void check_converges(float gamma_deg, float amp, float f)
{
  FbConfig cfg;
  FieldFb fb;
  Plant p;
  float alpha[4], beta[4], err0 = -1.0f, rip0, rip1, peak = 0.0f;
  int it, k = 0;
  /* Window = at least ~1.1 revolutions, as the firmware sizes it. */
  int win = (int)ceilf(1.1f * 50.0f / f);
  char msg[160];

  field_fb_default_config(&cfg);
  field_fb_init(&fb);
  plant_default(&p, -1);
  ideal_coeffs(gamma_deg, amp, alpha, beta);
  rip0 = true_ripple(&fb, &p, alpha, beta);
  if (win < 40) win = 40;
  for (it = 0; it < 20; it++) {
    run_window(&fb, &p, alpha, beta, f, k, win, &peak);
    k += win;
    expect(field_fb_update(&fb, &cfg, alpha, beta, 1) == 1, "window accepted");
    if (it == 0) err0 = fb.err;
  }
  /* One more estimate-only window to measure the converged error. */
  run_window(&fb, &p, alpha, beta, f, k, win, &peak);
  field_fb_update(&fb, &cfg, alpha, beta, 0);
  rip1 = true_ripple(&fb, &p, alpha, beta);

  snprintf(msg, sizeof(msg), "γ=%.0f amp=%.0f f=%.2f: err %.3f -> %.3f, ripple %.3f -> %.3f, peak %.1f",
           (double)gamma_deg, (double)amp, (double)f, (double)err0, (double)fb.err,
           (double)rip0, (double)rip1, (double)peak);
  printf("  %s\n", msg);
  expect(err0 > 0.08f, msg);              /* open loop is visibly off */
  expect(fb.err < 0.02f, msg);            /* closed loop is within 2 % */
  expect(rip1 < 0.03f && rip1 < 0.3f * rip0, msg);
  expect(fb.fault == 0, msg);
  expect(peak <= 255.0f * 1.02f, msg);    /* never asks for more than ±255 */
}

static void check_fault_on_flipped_sensor(void)
{
  FbConfig cfg;
  FieldFb fb;
  Plant p;
  float alpha[4], beta[4];
  int it, k = 0;

  field_fb_default_config(&cfg);
  field_fb_init(&fb);
  plant_default(&p, 1); /* M2 chip reads with the wrong sign (bad zsign) */
  ideal_coeffs(0.0f, 150.0f, alpha, beta);
  for (it = 0; it < 6; it++) {
    run_window(&fb, &p, alpha, beta, 1.0f, k, 60, NULL);
    k += 60;
    field_fb_update(&fb, &cfg, alpha, beta, 1);
  }
  expect((fb.fault & 0x2u) != 0, "flipped M2 sensor must be flagged");
  expect(fb.k[1].re > 0.5f, "flipped M2 sensor must not invert M2's drive");
}

static void check_alias_rejected(void)
{
  FbConfig cfg;
  FieldFb fb;
  Plant p;
  float alpha[4], beta[4];

  field_fb_default_config(&cfg);
  field_fb_init(&fb);
  plant_default(&p, -1);
  ideal_coeffs(45.0f, 120.0f, alpha, beta);
  /* 25 Hz at 50 Hz sampling: only θ = 0 and π are ever seen. */
  run_window(&fb, &p, alpha, beta, 25.0f, 0, 80, NULL);
  expect(field_fb_update(&fb, &cfg, alpha, beta, 1) == 0, "aliased window rejected");
  expect(fb.k[0].re == 1.0f && fb.k[0].im == 0.0f, "aliased window leaves K alone");
}

static void check_short_window_rejected(void)
{
  FbConfig cfg;
  FieldFb fb;
  Plant p;
  float alpha[4], beta[4];

  field_fb_default_config(&cfg);
  field_fb_init(&fb);
  plant_default(&p, -1);
  ideal_coeffs(45.0f, 200.0f, alpha, beta);
  /* 60 samples at 0.3 Hz = a third of a revolution: not enough phase spread. */
  run_window(&fb, &p, alpha, beta, 0.3f, 0, 60, NULL);
  expect(field_fb_update(&fb, &cfg, alpha, beta, 1) == 0, "partial-revolution window rejected");
}

static void check_estimate_only(void)
{
  FbConfig cfg;
  FieldFb fb;
  Plant p;
  float alpha[4], beta[4];
  int i;

  field_fb_default_config(&cfg);
  field_fb_init(&fb);
  plant_default(&p, -1);
  ideal_coeffs(90.0f, 150.0f, alpha, beta);
  run_window(&fb, &p, alpha, beta, 0.7f, 0, 80, NULL);
  expect(field_fb_update(&fb, &cfg, alpha, beta, 0) == 1, "estimate-only window used");
  expect(fb.ok && fb.err > 0.05f, "estimate-only reports open-loop error");
  for (i = 0; i < 4; i++) {
    expect(fb.k[i].re == 1.0f && fb.k[i].im == 0.0f, "estimate-only leaves K = 1");
  }
}

static void check_coil_formula(void)
{
  /* K = |K|e^{jψ} must equal |K|·u(θ+ψ). */
  FbCplx k = polar(0.8f, 25.0f);
  float a = 120.0f, b = -60.0f, th = 0.7f, psi = 25.0f * (PI_F / 180.0f);
  float got = field_fb_coil(k, a, b, cosf(th), sinf(th));
  float want = 0.8f * (a * cosf(th + psi) + b * sinf(th + psi));
  expect(fabsf(got - want) < 1e-3f, "field_fb_coil = |K| u(θ + arg K)");
}

int main(void)
{
  check_coil_formula();
  check_converges(0.0f, 150.0f, 1.0f);    /* revolve X */
  check_converges(90.0f, 150.0f, 1.0f);   /* revolve Z */
  check_converges(45.0f, 200.0f, 0.3f);   /* tilt */
  check_converges(135.0f, 120.0f, 13.0f); /* fast: coarse sampling */
  check_converges(0.0f, 255.0f, 1.0f);    /* full amp: headroom cap */
  check_fault_on_flipped_sensor();
  check_alias_rejected();
  check_short_window_rejected();
  check_estimate_only();

  if (g_fails) {
    fprintf(stderr, "%d check(s) failed\n", g_fails);
    return 1;
  }
  printf("ok: MLX feedback converges to the commanded circle, caps at ±255, rejects bad data\n");
  return 0;
}
