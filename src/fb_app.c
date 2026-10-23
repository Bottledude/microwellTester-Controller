#include "fb_app.h"
#include "field_engine.h"
#include "mlx_app.h"
#include "cli.h"

#include <math.h>

uint8_t  g_fb_en;
FieldFb  g_fb;
FbConfig g_fb_cfg;

/* What the current window was measured under; any change restarts it. */
static struct {
  uint8_t      valid;
  FieldProgram prog;
  RotateDir    dir;
  uint8_t      amp, h, v;
  float        gamma;
  float        freq;
} s_sig;

static float   s_last_theta;
static uint8_t s_last_valid;
static uint8_t s_cfg_init;

static void fb_cfg_once(void)
{
  if (s_cfg_init) return;
  field_fb_default_config(&g_fb_cfg);
  g_fb_cfg.umax = (float)PWM_ARR;
  field_fb_init(&g_fb);
  s_cfg_init = 1;
}

static void fb_push_k(void)
{
  Field_FbSetK(g_fb.k, g_rotate_dir);
}

void Fb_Reset(void)
{
  fb_cfg_once();
  field_fb_init(&g_fb);
  s_sig.valid = 0;
  fb_push_k();
}

uint8_t Fb_SetEnabled(uint8_t on)
{
  fb_cfg_once();
  if (on && g_mlx_n_present < NUM_COILS) return 0;
  g_fb_apply = 0;
  Fb_Reset();
  g_fb_en = on ? 1u : 0u;
  g_fb_apply = g_fb_en;
  return 1;
}

static uint32_t fb_window_samples(float freq)
{
  /* ≥ ~1.1 revolutions at MLX_POLL_HZ, at least 40 samples, at most ~2 min. */
  float n = 1.1f * (float)MLX_POLL_HZ / (freq > 0.001f ? freq : 0.001f);
  if (n < 40.0f) n = 40.0f;
  if (n > 120.0f * (float)MLX_POLL_HZ) n = 120.0f * (float)MLX_POLL_HZ;
  return (uint32_t)n;
}

void Fb_OnMlxSet(uint32_t phase, uint8_t steady_at_trigger,
                 const float m[4], uint8_t sat)
{
  float alpha[4], beta[4];

  fb_cfg_once();
  s_last_theta = Field_SynTheta(phase);
  s_last_valid = 1;

  if (!steady_at_trigger || !Field_FbSteady() || sat ||
      g_mlx_n_present < NUM_COILS) {
    field_fb_clear_window(&g_fb);
    return;
  }

  if (s_sig.valid) {
    if (s_sig.freq != g_rotate_freq_hz) {
      /* Coil lag depends on frequency: relearn from K = 1. */
      field_fb_init(&g_fb);
      fb_push_k();
    } else if (s_sig.dir != g_rotate_dir) {
      /* A time lag flips sign in θ when the direction reverses. */
      field_fb_conjugate(&g_fb);
      fb_push_k();
    } else if (s_sig.prog != g_field_prog || s_sig.amp != g_rotate_amp_live ||
               s_sig.h != g_revolve_h_pct || s_sig.v != g_revolve_v_pct ||
               s_sig.gamma != g_tilt_gamma_live) {
      field_fb_clear_window(&g_fb);
    }
  }
  s_sig.valid = 1;
  s_sig.prog = g_field_prog;
  s_sig.dir = g_rotate_dir;
  s_sig.amp = g_rotate_amp_live;
  s_sig.h = g_revolve_h_pct;
  s_sig.v = g_revolve_v_pct;
  s_sig.gamma = g_tilt_gamma_live;
  s_sig.freq = g_rotate_freq_hz;

  field_fb_add(&g_fb, s_last_theta, m);
  g_fb_cfg.min_samples = fb_window_samples(g_rotate_freq_hz);
  Field_IdealCoeffs(alpha, beta);
  if (field_fb_update(&g_fb, &g_fb_cfg, alpha, beta, g_fb_en) && g_fb_en) {
    fb_push_k();
  }
}

uint8_t Fb_Expected(float out[3])
{
  float alpha[4], beta[4];
  if (!s_cfg_init || !g_fb.ok || !s_last_valid || !g_rotate_active) return 0;
  Field_IdealCoeffs(alpha, beta);
  field_fb_expected(&g_fb, alpha, beta, s_last_theta, out);
  return 1;
}

void Fb_PrintStatus(void)
{
  int i;
  fb_cfg_once();
  uart_printf("fb=%s apply=%u est=%s err=%.2f%% updates=%lu window=%lu/%lu rate=%.2f%s\r\n",
              g_fb_en ? "on" : "off", (unsigned)g_fb_apply,
              g_fb.ok ? "ok" : "none", (double)(100.0f * g_fb.err),
              (unsigned long)g_fb.updates, (unsigned long)g_fb.n,
              (unsigned long)fb_window_samples(g_rotate_freq_hz),
              (double)g_fb_cfg.mu,
              Field_FbSteady() ? "" : "  (waiting: field not steady)");
  uart_printf("  gain %.5f mT/duty  phase %.1f deg\r\n",
              (double)fb_cabs(g_fb.gfit),
              (double)(fb_carg(g_fb.gfit) * 57.29578f));
  for (i = 0; i < NUM_COILS; i++) {
    uart_printf("  M%d |K|=%.3f arg=%+.1f deg  G=%.5f mT/duty @ %+.1f deg%s\r\n", i + 1,
                (double)fb_cabs(g_fb.k[i]), (double)(fb_carg(g_fb.k[i]) * 57.29578f),
                (double)fb_cabs(g_fb.g[i]), (double)(fb_carg(g_fb.g[i]) * 57.29578f),
                (g_fb.fault & (1u << i)) ? "  REFUSED (check zsign / chip)" : "");
  }
}
