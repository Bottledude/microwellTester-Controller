/**
 * @file field_fb.c
 * @brief Per-coil lock-in estimate and correction from the behind-coil chips.
 */
#include "field_fb.h"
#include "field_tilt.h"

#include <math.h>
#include <string.h>

static FbCplx c_make(float re, float im)
{
  FbCplx z;
  z.re = re;
  z.im = im;
  return z;
}

static FbCplx c_mul(FbCplx a, FbCplx b)
{
  return c_make(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}

static FbCplx c_div(FbCplx a, FbCplx b)
{
  float d = b.re * b.re + b.im * b.im;
  if (!(d > 0.0f)) return c_make(0.0f, 0.0f);
  return c_make((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}

static FbCplx c_scale(FbCplx a, float s)
{
  return c_make(a.re * s, a.im * s);
}

float fb_cabs(FbCplx a)
{
  return sqrtf(a.re * a.re + a.im * a.im);
}

float fb_carg(FbCplx a)
{
  return atan2f(a.im, a.re);
}

void field_fb_default_config(FbConfig *cfg)
{
  if (!cfg) return;
  cfg->mu = 0.5f;
  cfg->kmin = 0.25f;
  cfg->kmax = 2.5f;
  cfg->psi_max_rad = 75.0f * (3.14159265358979323846f / 180.0f);
  cfg->umax = 255.0f;
  cfg->min_drive = 4.0f;
  cfg->min_samples = 40u;
}

void field_fb_clear_window(FieldFb *fb)
{
  if (!fb) return;
  fb->n = 0;
  fb->scc = fb->sss = fb->scs = fb->sc = fb->ss = 0.0f;
  memset(fb->smc, 0, sizeof(fb->smc));
  memset(fb->sms, 0, sizeof(fb->sms));
  memset(fb->sm, 0, sizeof(fb->sm));
}

void field_fb_init(FieldFb *fb)
{
  int i;
  if (!fb) return;
  memset(fb, 0, sizeof(*fb));
  for (i = 0; i < FB_NCOIL; i++) fb->k[i] = c_make(1.0f, 0.0f);
}

void field_fb_conjugate(FieldFb *fb)
{
  int i;
  if (!fb) return;
  for (i = 0; i < FB_NCOIL; i++) {
    fb->k[i].im = -fb->k[i].im;
    fb->g[i].im = -fb->g[i].im;
  }
  fb->gfit.im = -fb->gfit.im;
  field_fb_clear_window(fb);
}

void field_fb_add(FieldFb *fb, float theta, const float m[FB_NCOIL])
{
  float c, s;
  int i;
  if (!fb || !m) return;
  c = cosf(theta);
  s = sinf(theta);
  fb->n++;
  fb->scc += c * c;
  fb->sss += s * s;
  fb->scs += c * s;
  fb->sc += c;
  fb->ss += s;
  for (i = 0; i < FB_NCOIL; i++) {
    fb->smc[i] += m[i] * c;
    fb->sms[i] += m[i] * s;
    fb->sm[i] += m[i];
  }
}

float field_fb_coil(FbCplx k, float alpha, float beta, float c, float s)
{
  return alpha * (k.re * c - k.im * s) + beta * (k.re * s + k.im * c);
}

/* |Σ z_i â_i| over the three complex lab components. */
static float field_norm(const FbCplx z[FB_NCOIL])
{
  float acc = 0.0f;
  int a, i;
  for (a = 0; a < 3; a++) {
    FbCplx v = c_make(0.0f, 0.0f);
    for (i = 0; i < FB_NCOIL; i++) {
      float ax[3];
      field_tilt_coil_axis((uint8_t)i, ax);
      v.re += z[i].re * ax[a];
      v.im += z[i].im * ax[a];
    }
    acc += v.re * v.re + v.im * v.im;
  }
  return sqrtf(acc);
}

int field_fb_update(FieldFb *fb, const FbConfig *cfg,
                    const float alpha[FB_NCOIL], const float beta[FB_NCOIL],
                    uint8_t adapt)
{
  float n, a11, a12, a13, a22, a23, a33, det;
  float i11, i12, i13, i22, i23;
  FbCplx U[FB_NCOIL], M[FB_NCOIL], E[FB_NCOIL], R[FB_NCOIL];
  FbCplx num = c_make(0.0f, 0.0f);
  float den = 0.0f, cap = 1.0e30f;
  uint8_t use[FB_NCOIL];
  int i;

  if (!fb || !cfg || !alpha || !beta) return 0;
  if (fb->n < cfg->min_samples || fb->n < 3u) return 0;

  /* Normal equations for x ≈ p cosθ + q sinθ + r (symmetric 3×3). */
  n = (float)fb->n;
  a11 = fb->scc; a12 = fb->scs; a13 = fb->sc;
  a22 = fb->sss; a23 = fb->ss;  a33 = n;
  i11 = a22 * a33 - a23 * a23;
  i12 = a13 * a23 - a12 * a33;
  i13 = a12 * a23 - a13 * a22;
  i22 = a11 * a33 - a13 * a13;
  i23 = a12 * a13 - a11 * a23;
  det = a11 * i11 + a12 * i12 + a13 * i13;
  /* Uniform phase coverage gives det/n³ ≈ 1/4; few distinct phases → ~0. */
  if (!(det / (n * n * n) > 0.02f)) {
    field_fb_clear_window(fb);
    return 0;
  }

  for (i = 0; i < FB_NCOIL; i++) {
    float bc = fb->smc[i], bs = fb->sms[i], b0 = fb->sm[i];
    float p = (i11 * bc + i12 * bs + i13 * b0) / det;
    float q = (i12 * bc + i22 * bs + i23 * b0) / det;
    M[i] = c_make(p, -q);
    U[i] = c_make(alpha[i], -beta[i]);
    use[i] = (fb_cabs(U[i]) >= cfg->min_drive) ? 1u : 0u;
    if (use[i]) {
      FbCplx d = c_mul(fb->k[i], U[i]);
      fb->g[i] = c_div(M[i], d);
      if (!(fb_cabs(fb->g[i]) > 1.0e-9f)) use[i] = 0;
    }
    /* Ĝ_fit numerator/denominator: Σ M U*, Σ |U|² */
    num.re += M[i].re * U[i].re + M[i].im * U[i].im;
    num.im += M[i].im * U[i].re - M[i].re * U[i].im;
    den += U[i].re * U[i].re + U[i].im * U[i].im;
  }
  field_fb_clear_window(fb);
  if (!(den > 0.0f)) return 0;

  fb->gfit = c_scale(num, 1.0f / den);
  for (i = 0; i < FB_NCOIL; i++) {
    E[i] = c_mul(fb->gfit, U[i]);
    R[i] = c_make(M[i].re - E[i].re, M[i].im - E[i].im);
  }
  {
    float en = field_norm(E);
    fb->err = (en > 0.0f) ? field_norm(R) / en : 0.0f;
  }
  fb->ok = 1;
  fb->updates++;
  if (!adapt) return 1;

  /* Target Ĝ: same phase as Ĝ_fit, magnitude capped by the weakest coil's headroom. */
  for (i = 0; i < FB_NCOIL; i++) {
    float lim;
    if (!use[i]) continue;
    lim = fb_cabs(fb->g[i]) * cfg->umax / fb_cabs(U[i]);
    if (lim < cap) cap = lim;
  }
  {
    FbCplx tgt = fb->gfit;
    float mag = fb_cabs(tgt);
    if (mag > cap && mag > 0.0f) tgt = c_scale(tgt, cap / mag);
    fb->fault = 0;
    for (i = 0; i < FB_NCOIL; i++) {
      FbCplx kn;
      float km;
      if (!use[i]) continue;
      kn = c_div(tgt, fb->g[i]);
      km = fb_cabs(kn);
      if (fabsf(fb_carg(kn)) > cfg->psi_max_rad || km > cfg->kmax || km < cfg->kmin) {
        fb->fault |= (uint8_t)(1u << i);
        continue;
      }
      fb->k[i].re += cfg->mu * (kn.re - fb->k[i].re);
      fb->k[i].im += cfg->mu * (kn.im - fb->k[i].im);
    }
  }
  return 1;
}

void field_fb_expected(const FieldFb *fb, const float alpha[FB_NCOIL],
                       const float beta[FB_NCOIL], float theta, float out[3])
{
  FbCplx e;
  float c, s, u[FB_NCOIL];
  int i;

  if (!out) return;
  out[0] = out[1] = out[2] = 0.0f;
  if (!fb || !fb->ok || !alpha || !beta) return;
  c = cosf(theta);
  s = sinf(theta);
  for (i = 0; i < FB_NCOIL; i++) {
    /* Re{Ĝ U e^{jθ}} = the ideal coil waveform scaled/shifted by Ĝ. */
    e = fb->gfit;
    u[i] = field_fb_coil(e, alpha[i], beta[i], c, s);
  }
  field_tilt_forward(u, &out[0], &out[1], &out[2]);
}
