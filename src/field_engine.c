#include "field_engine.h"
#include "coil_output.h"
#include "platform_lock.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

volatile uint8_t  g_rotate_active;
float             g_rotate_freq_hz = 1.0f;
uint8_t           g_rotate_amp = 0;
volatile uint8_t  g_rotate_amp_live = 0;
RotateDir         g_rotate_dir = ROT_CW;
RotateDir         g_rotate_dir_want = ROT_CW;
FieldProgram      g_field_prog = FIELD_ROTATE_Y;
FieldProgram      g_field_prog_want = FIELD_ROTATE_Y;
uint8_t           g_revolve_h_pct = (uint8_t)REVOLVE_H_PCT_DEFAULT;
uint8_t           g_revolve_v_pct = (uint8_t)REVOLVE_V_PCT_DEFAULT;
float             g_tilt_gamma_want = TILT_GAMMA_DEFAULT_DEG;
volatile float    g_tilt_gamma_live = TILT_GAMMA_DEFAULT_DEG;
volatile float    g_tilt_sin_g = FIELD_INV_SQRT2;
volatile float    g_tilt_cos_g = FIELD_INV_SQRT2;
volatile float    g_tilt_scale = 1.0f;
volatile uint8_t  g_rotate_stop_pending;
volatile uint32_t g_phase;
volatile uint32_t g_phase_inc;
volatile uint8_t  g_revolve_phase_hold;
volatile uint8_t  g_revolve_env_peak;
volatile uint8_t  g_revolve_park;
volatile uint8_t  g_revolve_corner;
volatile uint8_t  g_revolve_travel;
volatile uint8_t  g_aim_hold;
volatile uint32_t g_aim_elec;
volatile uint8_t  g_fb_apply;
/* MLX feedback correction per coil, time convention (CCW conjugates it). */
static FbCplx     s_fb_k[NUM_COILS] = {
  {1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}
};

/* Q15 sine table: sin(2π i / 360) * 32767 */
static const int16_t SINE_LUT[SINE_LUT_SIZE] = {
        0,     572,    1144,    1715,    2286,    2856,    3425,    3993,
     4560,    5126,    5690,    6252,    6813,    7371,    7927,    8481,
     9032,    9580,   10126,   10668,   11207,   11743,   12275,   12803,
    13328,   13848,   14364,   14876,   15383,   15886,   16383,   16876,
    17364,   17846,   18323,   18794,   19260,   19720,   20173,   20621,
    21062,   21497,   21925,   22347,   22762,   23170,   23571,   23964,
    24351,   24730,   25101,   25465,   25821,   26169,   26509,   26841,
    27165,   27481,   27788,   28087,   28377,   28659,   28932,   29196,
    29451,   29697,   29934,   30162,   30381,   30591,   30791,   30982,
    31163,   31335,   31498,   31650,   31794,   31927,   32051,   32165,
    32269,   32364,   32448,   32523,   32587,   32642,   32687,   32722,
    32747,   32762,   32767,   32762,   32747,   32722,   32687,   32642,
    32587,   32523,   32448,   32364,   32269,   32165,   32051,   31927,
    31794,   31650,   31498,   31335,   31163,   30982,   30791,   30591,
    30381,   30162,   29934,   29697,   29451,   29196,   28932,   28659,
    28377,   28087,   27788,   27481,   27165,   26841,   26509,   26169,
    25821,   25465,   25101,   24730,   24351,   23964,   23571,   23170,
    22762,   22347,   21925,   21497,   21062,   20621,   20173,   19720,
    19260,   18794,   18323,   17846,   17364,   16876,   16383,   15886,
    15383,   14876,   14364,   13848,   13328,   12803,   12275,   11743,
    11207,   10668,   10126,    9580,    9032,    8481,    7927,    7371,
     6813,    6252,    5690,    5126,    4560,    3993,    3425,    2856,
     2286,    1715,    1144,     572,       0,    -572,   -1144,   -1715,
    -2286,   -2856,   -3425,   -3993,   -4560,   -5126,   -5690,   -6252,
    -6813,   -7371,   -7927,   -8481,   -9032,   -9580,  -10126,  -10668,
   -11207,  -11743,  -12275,  -12803,  -13328,  -13848,  -14364,  -14876,
   -15383,  -15886,  -16384,  -16876,  -17364,  -17846,  -18323,  -18794,
   -19260,  -19720,  -20173,  -20621,  -21062,  -21497,  -21925,  -22347,
   -22762,  -23170,  -23571,  -23964,  -24351,  -24730,  -25101,  -25465,
   -25821,  -26169,  -26509,  -26841,  -27165,  -27481,  -27788,  -28087,
   -28377,  -28659,  -28932,  -29196,  -29451,  -29697,  -29934,  -30162,
   -30381,  -30591,  -30791,  -30982,  -31163,  -31335,  -31498,  -31650,
   -31794,  -31927,  -32051,  -32165,  -32269,  -32364,  -32448,  -32523,
   -32587,  -32642,  -32687,  -32722,  -32747,  -32762,  -32767,  -32762,
   -32747,  -32722,  -32687,  -32642,  -32587,  -32523,  -32448,  -32364,
   -32269,  -32165,  -32051,  -31927,  -31794,  -31650,  -31498,  -31335,
   -31163,  -30982,  -30791,  -30591,  -30381,  -30162,  -29934,  -29697,
   -29451,  -29196,  -28932,  -28659,  -28377,  -28087,  -27788,  -27481,
   -27165,  -26841,  -26509,  -26169,  -25821,  -25465,  -25101,  -24730,
   -24351,  -23964,  -23571,  -23170,  -22762,  -22347,  -21925,  -21497,
   -21062,  -20621,  -20173,  -19720,  -19260,  -18794,  -18323,  -17846,
   -17364,  -16876,  -16384,  -15886,  -15383,  -14876,  -14364,  -13848,
   -13328,  -12803,  -12275,  -11743,  -11207,  -10668,  -10126,   -9580,
    -9032,   -8481,   -7927,   -7371,   -6813,   -6252,   -5690,   -5126,
    -4560,   -3993,   -3425,   -2856,   -2286,   -1715,   -1144,    -572
};

const char *rotate_dir_str(RotateDir d)
{
  return d == ROT_CW ? "cw" : "ccw";
}

const char *field_program_str(FieldProgram p)
{
  if (p == FIELD_REVOLVE_X || p == FIELD_REVOLVE_Z || p == FIELD_REVOLVE_TILT)
    return "revolve";
  return "rotate";
}

const char *field_axis_str(FieldProgram p)
{
  if (p == FIELD_REVOLVE_X) return "x";
  if (p == FIELD_REVOLVE_Z) return "z";
  if (p == FIELD_REVOLVE_TILT) return "tilt";
  return "y";
}

uint8_t field_is_revolve(FieldProgram p)
{
  return (p == FIELD_REVOLVE_X || p == FIELD_REVOLVE_Z ||
          p == FIELD_REVOLVE_TILT) ? 1 : 0;
}

uint8_t field_is_tilt(FieldProgram p)
{
  return (p == FIELD_REVOLVE_TILT) ? 1 : 0;
}

uint8_t clamp_u8_pct(int v)
{
  if (v < (int)REVOLVE_HV_PCT_MIN) return (uint8_t)REVOLVE_HV_PCT_MIN;
  if (v > (int)REVOLVE_HV_PCT_MAX) return (uint8_t)REVOLVE_HV_PCT_MAX;
  return (uint8_t)v;
}

void Rotate_RecalcPhaseInc(void)
{
  float f = g_rotate_freq_hz;
  double inc;
  if (f < ROTATE_FREQ_MIN_HZ) f = ROTATE_FREQ_MIN_HZ;
  if (f > ROTATE_FREQ_MAX_HZ) f = ROTATE_FREQ_MAX_HZ;
  g_rotate_freq_hz = f;
  inc = ((double)f / (double)SINE_UPDATE_HZ) * 4294967296.0;
  if (inc < 1.0) inc = 1.0;
  if (inc > 4294967295.0) inc = 4294967295.0;
  g_phase_inc = (uint32_t)inc;
}

static uint16_t lut_from_phase(uint32_t ph)
{
  return (uint16_t)(((uint64_t)ph * (uint32_t)SINE_LUT_SIZE) >> 32);
}

static uint16_t lut_mod(uint16_t idx)
{
  return (uint16_t)(idx % (uint16_t)SINE_LUT_SIZE);
}

static uint16_t lut_add(uint16_t idx, int16_t delta)
{
  int32_t v = (int32_t)lut_mod(idx) + (int32_t)delta;
  int32_t n = (int32_t)SINE_LUT_SIZE;
  v %= n;
  if (v < 0) v += n;
  return (uint16_t)v;
}

static int16_t sine_scaled(uint16_t idx, uint8_t amp)
{
  idx = lut_mod(idx);
  return (int16_t)((SINE_LUT[idx] * (int32_t)amp) >> 15);
}

static uint8_t revolve_scaled_amp(uint8_t amp, uint8_t pct)
{
  uint32_t v = ((uint32_t)amp * (uint32_t)pct + 50u) / 100u;
  if (v > 255u) v = 255u;
  return (uint8_t)v;
}

static uint8_t revolve_envelope_amp(uint8_t live, uint8_t peak)
{
  int16_t s;
  uint8_t idx;
  if (peak == 0) return 0;
  if (live >= peak) return peak;
  idx = (uint8_t)(((uint32_t)SINE_LUT_90 * (uint32_t)live) / (uint32_t)peak);
  s = sine_scaled(idx, peak);
  if (s < 0) s = 0;
  if (s > 255) s = 255;
  return (uint8_t)s;
}

static void field_prog_sincos(FieldProgram p, float live_sg, float live_cg,
                              float *sg, float *cg)
{
  if (p == FIELD_REVOLVE_X) {
    *sg = 0.0f;
    *cg = 1.0f;
  } else if (p == FIELD_REVOLVE_Z) {
    *sg = 1.0f;
    *cg = 0.0f;
  } else {
    *sg = live_sg;
    *cg = live_cg;
  }
}

void Tilt_CommitGamma(float deg)
{
  FieldTiltCoeffs c;
  field_tilt_coeffs_from_deg(deg, &c);
  field_irq_lock();
  g_tilt_gamma_live = c.deg;
  g_tilt_sin_g = c.sin_g;
  g_tilt_cos_g = c.cos_g;
  field_irq_unlock();
}

void Tilt_SetGamma(float deg)
{
  g_tilt_gamma_want = deg;
  if (!g_rotate_active || !field_is_tilt(g_field_prog)) {
    Tilt_CommitGamma(deg);
  }
}

float Revolve_PreviewGain(void)
{
  float sg, cg, want_sg, want_cg;
  field_tilt_gamma_sincos(g_tilt_gamma_want, &want_sg, &want_cg);
  field_prog_sincos(g_field_prog_want, want_sg, want_cg, &sg, &cg);
  return field_tilt_max_gain(sg, cg);
}

float Tilt_PreviewScale(uint8_t amp)
{
  uint8_t ah = revolve_scaled_amp(amp, g_revolve_h_pct);
  uint8_t av = revolve_scaled_amp(amp, g_revolve_v_pct);
  float sg, cg, want_sg, want_cg, gain;
  field_tilt_gamma_sincos(g_tilt_gamma_want, &want_sg, &want_cg);
  field_prog_sincos(g_field_prog_want, want_sg, want_cg, &sg, &cg);
  gain = field_tilt_max_gain(sg, cg);
  return field_tilt_shared_scale((float)ah * gain, (float)av * gain, sg, cg,
                                 (float)PWM_ARR);
}

/*
 * amp_h / amp_v are 0..255 duty-scale radii. They are multiplied by the
 * plane's max gain so 255 (rev_hv 100/100) is the largest constant-|B|
 * circle the four coils can hold with pure-sine drive: |B| = amp · gain
 * (1.5 for revolve X/Z, 1.549 at γ=45). At that point all four coils peak
 * at ±255 together.
 */
static void revolve_tilt_currents(uint32_t phase, uint8_t amp_h, uint8_t amp_v,
                                  RotateDir dir, float sin_g, float cos_g,
                                  int16_t out[4], float *scale_out)
{
  uint32_t ph = phase;
  uint16_t si;
  float invs = 1.0f / 32767.0f;
  float s, c, raw[4], scale;
  float gain = field_tilt_max_gain(sin_g, cos_g);

  if (dir == ROT_CCW) ph = 0u - ph;
  si = lut_from_phase(ph);
  s = (float)SINE_LUT[si] * invs;
  c = (float)SINE_LUT[lut_add(si, (int16_t)SINE_LUT_90)] * invs;
  field_tilt_commands((float)amp_h * gain, (float)amp_v * gain, s, c,
                      sin_g, cos_g, (float)PWM_ARR, raw, &scale);
  field_tilt_quantize(raw, out, (int16_t)PWM_ARR);
  if (scale_out) *scale_out = scale;
}

/*
 * Every program is one sinusoid per coil in the synthesis phase θ
 * (θ = phase for CW, −phase for CCW): u_i = α_i cosθ + β_i sinθ.
 * Rotate Y: M1=M3 = a cosθ, M2=M4 = a sinθ (CCW's −sin is θ → −θ).
 * Revolve: α = commands at θ=0, β = commands at θ=90° (the law is linear).
 */
static void field_coeffs(FieldProgram prog, uint8_t a_cmd, float sg, float cg,
                         float alpha[4], float beta[4], float *scale_out)
{
  float scale = 1.0f;
  if (prog == FIELD_ROTATE_Y) {
    float a = (float)a_cmd;
    alpha[0] = a; alpha[1] = 0.0f; alpha[2] = a; alpha[3] = 0.0f;
    beta[0] = 0.0f; beta[1] = a; beta[2] = 0.0f; beta[3] = a;
  } else {
    float gain = field_tilt_max_gain(sg, cg);
    float ah = (float)revolve_scaled_amp(a_cmd, g_revolve_h_pct) * gain;
    float av = (float)revolve_scaled_amp(a_cmd, g_revolve_v_pct) * gain;
    field_tilt_commands(ah, av, 0.0f, 1.0f, sg, cg, (float)PWM_ARR, alpha, &scale);
    field_tilt_commands(ah, av, 1.0f, 0.0f, sg, cg, (float)PWM_ARR, beta, NULL);
  }
  if (scale_out) *scale_out = scale;
}

void Field_IdealCoeffs(float alpha[4], float beta[4])
{
  float sg, cg;
  field_prog_sincos(g_field_prog, g_tilt_sin_g, g_tilt_cos_g, &sg, &cg);
  field_coeffs(g_field_prog, g_rotate_amp_live, sg, cg, alpha, beta, NULL);
}

float Field_SynTheta(uint32_t phase)
{
  uint32_t ph = (g_rotate_dir == ROT_CCW) ? (0u - phase) : phase;
  return (float)ph * (6.28318530717958647692f / 4294967296.0f);
}

uint8_t Field_FbSteady(void)
{
  if (!g_rotate_active || g_rotate_stop_pending) return 0;
  if (g_rotate_amp_live == 0 || g_rotate_amp_live != g_rotate_amp) return 0;
  if (g_field_prog != g_field_prog_want || g_rotate_dir != g_rotate_dir_want) return 0;
  if (g_revolve_phase_hold || g_revolve_park || g_revolve_travel || g_aim_hold) return 0;
  if (field_is_tilt(g_field_prog) && g_tilt_gamma_live != g_tilt_gamma_want) return 0;
  return 1;
}

void Field_FbSetK(const FbCplx k[4], RotateDir dir)
{
  int i;
  field_irq_lock();
  for (i = 0; i < NUM_COILS; i++) {
    s_fb_k[i].re = k[i].re;
    s_fb_k[i].im = (dir == ROT_CCW) ? -k[i].im : k[i].im;
  }
  field_irq_unlock();
}

/* Same programs as the open-loop paths, with the per-coil MLX correction. */
static void fb_corrected_sample(FieldProgram prog, uint8_t hold, uint8_t amp)
{
  const float invs = 1.0f / 32767.0f;
  float alpha[4], beta[4], raw[4], sg, cg, scale, c, s, pk = 0.0f, sc = 1.0f;
  FbCplx k[4];
  uint32_t ph = g_phase;
  uint8_t a_cmd = amp;
  uint16_t si;
  int16_t cmd[4];
  int i;

  if (field_is_revolve(prog) && hold) a_cmd = revolve_envelope_amp(amp, g_revolve_env_peak);
  field_prog_sincos(prog, g_tilt_sin_g, g_tilt_cos_g, &sg, &cg);
  field_coeffs(prog, a_cmd, sg, cg, alpha, beta, &scale);
  if (g_rotate_dir == ROT_CCW) ph = 0u - ph;
  si = lut_from_phase(ph);
  s = (float)SINE_LUT[si] * invs;
  c = (float)SINE_LUT[lut_add(si, (int16_t)SINE_LUT_90)] * invs;

  field_irq_lock();
  for (i = 0; i < 4; i++) {
    k[i] = s_fb_k[i];
    if (g_rotate_dir == ROT_CCW) k[i].im = -k[i].im;
  }
  field_irq_unlock();

  /* One shared scale if the correction would push any coil past ±255. */
  for (i = 0; i < 4; i++) {
    float p = fb_cabs(k[i]) * sqrtf(alpha[i] * alpha[i] + beta[i] * beta[i]);
    if (p > pk) pk = p;
  }
  if (pk > (float)PWM_ARR) sc = (float)PWM_ARR / pk;
  for (i = 0; i < 4; i++) raw[i] = sc * field_fb_coil(k[i], alpha[i], beta[i], c, s);
  field_tilt_quantize(raw, cmd, (int16_t)PWM_ARR);
  if (field_is_revolve(prog)) g_tilt_scale = scale * sc;
  for (i = 0; i < 4; i++) Coil_ApplySignedDuty((uint8_t)i, cmd[i]);
}

static uint32_t revolve_corner_phase(uint8_t q, RotateDir dir)
{
  uint32_t ph = ((uint32_t)(q & 3u)) << 30;
  if (dir == ROT_CCW) ph = 0u - ph;
  return ph;
}

void Revolve_ClearPark(void)
{
  g_revolve_park = 0;
  g_revolve_travel = 0;
}

/* Shortest-arc step at the fastest rotate the rig already uses (50 Hz). */
static uint32_t aim_slew_step(void)
{
  double inc = ((double)ROTATE_FREQ_MAX_HZ / (double)SINE_UPDATE_HZ) * 4294967296.0;
  if (inc < 1.0) inc = 1.0;
  if (inc > 4294967295.0) inc = 4294967295.0;
  return (uint32_t)inc;
}

static void phase_slew_toward(uint32_t tgt, uint32_t step)
{
  uint32_t ahead = tgt - g_phase;
  if (ahead == 0u) return;
  if (ahead <= 0x80000000u) {
    if (ahead <= step) g_phase = tgt;
    else g_phase += step;
  } else {
    uint32_t back = 0u - ahead;
    if (back <= step) g_phase = tgt;
    else g_phase -= step;
  }
}

void Field_AimDeg(float deg)
{
  double d = fmod((double)deg, 360.0);
  if (d < 0.0) d += 360.0;
  if (d >= 360.0) d = 0.0;
  g_aim_elec = (uint32_t)(d * (4294967296.0 / 360.0));
  g_revolve_travel = 0;
  g_aim_hold = 1;
}

void Field_AimRun(void)
{
  uint8_t steady;
  g_aim_hold = 0;
  if (!g_revolve_park) return;
  Revolve_ClearPark();
  steady = (g_rotate_active && !g_rotate_stop_pending &&
            g_rotate_amp_live == g_rotate_amp &&
            g_field_prog == g_field_prog_want &&
            g_rotate_dir == g_rotate_dir_want) ? 1 : 0;
  if (steady && field_is_tilt(g_field_prog) &&
      g_tilt_gamma_live != g_tilt_gamma_want) {
    steady = 0;
  }
  if (steady) g_revolve_phase_hold = 0;
}

void Revolve_GotoCorner(uint8_t q)
{
  FieldProgram p = g_field_prog_want;
  uint32_t tgt;

  q &= 3u;
  if (!field_is_revolve(p)) {
    if (field_is_revolve(g_field_prog)) p = g_field_prog;
    else p = FIELD_REVOLVE_Z;
    Field_SetProgram(p);
  }

  g_revolve_park = 1;
  g_revolve_corner = q;
  tgt = revolve_corner_phase(q, g_rotate_dir);

  if (!g_rotate_active) {
    Field_Start(p);
    g_phase = tgt;
    g_revolve_travel = 0;
    g_revolve_phase_hold = 1;
    g_revolve_env_peak = g_rotate_amp;
    return;
  }

  if (!field_is_revolve(g_field_prog) || g_field_prog != p) {
    g_revolve_travel = 0;
    return;
  }

  if (g_phase == tgt) {
    g_revolve_travel = 0;
    g_revolve_phase_hold = 1;
    return;
  }

  if (g_revolve_phase_hold && g_rotate_amp_live != g_rotate_amp) {
    g_phase = tgt;
    g_revolve_travel = 0;
    return;
  }

  g_revolve_travel = 1;
  g_revolve_phase_hold = 0;
}

static uint8_t rotate_step_toward(uint8_t cur, uint8_t tgt)
{
  uint8_t step;
  if (cur == tgt) return cur;
  step = (uint8_t)ROTATE_RAMP_STEP;
  if (step < 1u) step = 1u;
  if (cur < tgt) {
    uint8_t d = (uint8_t)(tgt - cur);
    return (uint8_t)(cur + (d < step ? d : step));
  }
  {
    uint8_t d = (uint8_t)(cur - tgt);
    return (uint8_t)(cur - (d < step ? d : step));
  }
}

void Rotate_Stop(void)
{
  int n;
  int i;
  g_rotate_active = 0;
  g_rotate_stop_pending = 0;
  g_contour_active = 0;
  g_rotate_amp_live = 0;
  g_revolve_phase_hold = 0;
  g_revolve_env_peak = 0;
  Revolve_ClearPark();
  g_rotate_dir = g_rotate_dir_want;
  g_field_prog = g_field_prog_want;
  Tilt_CommitGamma(g_tilt_gamma_want);

  n = (NUM_COILS < NUM_ROTATE_COILS) ? NUM_COILS : NUM_ROTATE_COILS;
  for (i = 0; i < n; i++) {
    coils[i].enabled = 0;
    coils[i].duty_applied = 0;
    Coil_ApplyOutputs((uint8_t)i);
  }
}

void Rotate_RequestStop(void)
{
  if (!g_rotate_active) {
    Rotate_Stop();
    return;
  }
  g_rotate_stop_pending = 1;
}

void Rotate_SetDir(RotateDir d)
{
  g_rotate_dir_want = d;
  if (!g_rotate_active) g_rotate_dir = d;
}

void Field_SetProgram(FieldProgram p)
{
  g_field_prog_want = p;
  if (!g_rotate_active) g_field_prog = p;
}

void Field_ApplyOptionalFreqAmp(int argc, char **argv, int start)
{
  if (start < argc && argv[start] && argv[start][0]) {
    float f = (float)atof(argv[start]);
    if (f < ROTATE_FREQ_MIN_HZ) f = ROTATE_FREQ_MIN_HZ;
    if (f > ROTATE_FREQ_MAX_HZ) f = ROTATE_FREQ_MAX_HZ;
    g_rotate_freq_hz = f;
    Rotate_RecalcPhaseInc();
  }
  if ((start + 1) < argc && argv[start + 1] && argv[start + 1][0]) {
    int a = atoi(argv[start + 1]);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    g_rotate_amp = (uint8_t)a;
    {
      int i;
      for (i = 0; i < NUM_ROTATE_COILS && i < NUM_COILS; i++) {
        coils[i].magnitude = g_rotate_amp;
      }
    }
  }
}

void Field_Start(FieldProgram p)
{
  int i;
  g_contour_active = 0;
  g_rotate_stop_pending = 0;
  g_field_prog_want = p;
  Rotate_RecalcPhaseInc();

  if (g_rotate_active) return;

  if (g_revolve_park && field_is_revolve(g_field_prog_want)) {
    g_phase = revolve_corner_phase(g_revolve_corner, g_rotate_dir_want);
  } else {
    g_phase = 0;
    Revolve_ClearPark();
  }
  g_rotate_amp_live = 0;
  g_rotate_dir = g_rotate_dir_want;
  g_field_prog = g_field_prog_want;
  if (field_is_tilt(g_field_prog)) {
    Tilt_CommitGamma(g_tilt_gamma_want);
  }
  g_revolve_phase_hold = field_is_revolve(g_field_prog) ? 1 : 0;
  g_revolve_env_peak = g_rotate_amp;

  for (i = 0; i < NUM_ROTATE_COILS; i++) {
    coils[i].enabled = 1;
    coils[i].magnitude = g_rotate_amp;
  }

  g_rotate_active = 1;
}

void Rotate_RampUpdate(void)
{
  uint8_t prog_mismatch;
  uint8_t dir_mismatch;
  uint8_t gamma_mismatch = 0;
  uint8_t target;
  uint8_t is_rev;

  if (!g_rotate_active) return;

  prog_mismatch = (g_field_prog != g_field_prog_want);
  dir_mismatch = (g_rotate_dir != g_rotate_dir_want);
  if (field_is_tilt(g_field_prog) || field_is_tilt(g_field_prog_want)) {
    gamma_mismatch = (g_tilt_gamma_live != g_tilt_gamma_want) ? 1 : 0;
  }

  if ((dir_mismatch || prog_mismatch) && g_rotate_amp_live == 0 &&
      !g_rotate_stop_pending) {
    g_rotate_dir = g_rotate_dir_want;
    g_field_prog = g_field_prog_want;
    dir_mismatch = 0;
    prog_mismatch = 0;
    if (field_is_tilt(g_field_prog)) {
      Tilt_CommitGamma(g_tilt_gamma_want);
      gamma_mismatch = 0;
    }
    if (field_is_revolve(g_field_prog)) {
      if (g_revolve_park) {
        g_phase = revolve_corner_phase(g_revolve_corner, g_rotate_dir);
        g_revolve_travel = 0;
      } else {
        g_phase = 0;
      }
      g_revolve_phase_hold = 1;
      g_revolve_env_peak = g_rotate_amp;
    } else {
      g_revolve_phase_hold = 0;
      Revolve_ClearPark();
    }
  }

  if (gamma_mismatch && g_rotate_amp_live == 0 && !g_rotate_stop_pending) {
    Tilt_CommitGamma(g_tilt_gamma_want);
    gamma_mismatch = 0;
    if (field_is_revolve(g_field_prog)) {
      g_revolve_phase_hold = 1;
      g_revolve_env_peak = g_rotate_amp;
    }
  }

  target = g_rotate_amp;
  if (g_rotate_stop_pending || dir_mismatch || prog_mismatch || gamma_mismatch) {
    target = 0;
  }

  g_rotate_amp_live = rotate_step_toward(g_rotate_amp_live, target);

  is_rev = field_is_revolve(g_field_prog);
  if (!is_rev) {
    g_revolve_phase_hold = 0;
  } else if (g_rotate_stop_pending || dir_mismatch || prog_mismatch ||
             gamma_mismatch) {
    if (!g_revolve_phase_hold) {
      g_revolve_env_peak = g_rotate_amp_live;
    }
    g_revolve_phase_hold = 1;
  } else if (g_revolve_park && !g_revolve_travel) {
    g_revolve_phase_hold = 1;
  } else if (g_rotate_amp_live == target && !g_revolve_travel) {
    g_revolve_phase_hold = 0;
  }

  if (g_rotate_stop_pending && g_rotate_amp_live == 0) {
    Rotate_Stop();
  }
}

void Rotate_StopIfDcConflict(uint8_t idx)
{
  if (g_rotate_active && idx < NUM_ROTATE_COILS) {
    Rotate_Stop();
  }
}

void Field_SampleTick(void)
{
  FieldProgram prog;
  uint8_t hold;
  uint8_t travel;
  uint16_t si;
  uint8_t amp;

  if (!g_rotate_active) return;

  prog = g_field_prog;
  hold = g_revolve_phase_hold;
  travel = g_revolve_travel;
  if (g_aim_hold) {
    uint32_t tgt = g_aim_elec;
    if (g_rotate_dir == ROT_CCW) tgt = 0u - tgt;
    phase_slew_toward(tgt, aim_slew_step());
  } else if (prog == FIELD_ROTATE_Y) {
    g_phase += g_phase_inc;
  } else if (travel) {
    uint32_t prev = g_phase;
    uint32_t tgt = revolve_corner_phase(g_revolve_corner, g_rotate_dir);
    uint32_t step, off;
    g_phase += g_phase_inc;
    step = g_phase - prev;
    off = tgt - prev;
    if (off > 0u && off <= step) {
      g_phase = tgt;
      g_revolve_travel = 0;
      g_revolve_phase_hold = 1;
    }
  } else if (!hold) {
    g_phase += g_phase_inc;
  }

  si = lut_from_phase(g_phase);
  amp = g_rotate_amp_live;

  if (g_fb_apply) {
    fb_corrected_sample(prog, hold, amp);
    return;
  }

  if (prog == FIELD_ROTATE_Y) {
    int16_t d13 = sine_scaled(lut_add(si, (int16_t)SINE_LUT_90), amp);
    int16_t d24 = sine_scaled(si, amp);
    if (g_rotate_dir == ROT_CCW) d24 = (int16_t)(-d24);
    Coil_ApplyPairedSignedDuty(0, 2, d13);
    Coil_ApplyPairedSignedDuty(1, 3, d24);
  } else {
    uint8_t a_cmd = amp;
    uint8_t amp_h, amp_v;
    int16_t cmd[4];
    float sg, cg, scale;
    if (hold) a_cmd = revolve_envelope_amp(amp, g_revolve_env_peak);
    amp_h = revolve_scaled_amp(a_cmd, g_revolve_h_pct);
    amp_v = revolve_scaled_amp(a_cmd, g_revolve_v_pct);
    field_prog_sincos(prog, g_tilt_sin_g, g_tilt_cos_g, &sg, &cg);
    revolve_tilt_currents(g_phase, amp_h, amp_v, g_rotate_dir, sg, cg,
                          cmd, &scale);
    g_tilt_scale = scale;
    Coil_ApplySignedDuty(0, cmd[0]);
    Coil_ApplySignedDuty(1, cmd[1]);
    Coil_ApplySignedDuty(2, cmd[2]);
    Coil_ApplySignedDuty(3, cmd[3]);
  }
}

void Field_EngineInit(void)
{
  memset(coils, 0, sizeof(coils));
  Rotate_RecalcPhaseInc();
  g_rotate_amp_live = 0;
  Tilt_CommitGamma(g_tilt_gamma_want);
}
