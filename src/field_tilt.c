/**
 * @file field_tilt.c
 * @brief Tilted-axis four-coil inverse (30° tilt, equal vertical split).
 */
#include "field_tilt.h"

#include <stdlib.h>
#include <math.h>

int field_tilt_parse_gamma(const char *s, float *out)
{
  char *end = NULL;
  float v;

  if (!s || !*s) return 0;
  v = strtof(s, &end);
  if (end == s) return 0;
  while (*end == ' ' || *end == '\t') end++;
  if (*end != '\0') return 0;
  if (v != v) return 0; /* NaN */
  if (v < FIELD_TILT_GAMMA_MIN || v > FIELD_TILT_GAMMA_MAX) return 0;
  if (out) *out = v;
  return 1;
}

void field_tilt_gamma_sincos(float gamma_deg, float *sin_g, float *cos_g)
{
  float r, sg, cg;

  if (gamma_deg <= FIELD_TILT_GAMMA_MIN) {
    sg = 0.0f;
    cg = 1.0f;
  } else if (gamma_deg == 90.0f) {
    sg = 1.0f;
    cg = 0.0f;
  } else if (gamma_deg >= FIELD_TILT_GAMMA_MAX) {
    sg = 0.0f;
    cg = -1.0f;
  } else {
    r = gamma_deg * (3.14159265358979323846f / 180.0f);
    sg = sinf(r);
    cg = cosf(r);
  }
  if (sin_g) *sin_g = sg;
  if (cos_g) *cos_g = cg;
}

void field_tilt_coeffs_from_deg(float gamma_deg, FieldTiltCoeffs *out)
{
  FieldTiltCoeffs tmp;

  if (!out) return;
  tmp.deg = gamma_deg;
  field_tilt_gamma_sincos(gamma_deg, &tmp.sin_g, &tmp.cos_g);
  *out = tmp;
}

void field_tilt_weights(float sin_g, float cos_g, FieldTiltWeights *w)
{
  float cos2g, wv, dw;

  if (!w) return;
  cos2g = cos_g * cos_g - sin_g * sin_g;
  wv = 1.0f / (4.0f * FIELD_CV);                           /* even V share */
  dw = cos2g * FIELD_CV / (4.0f * FIELD_CH * FIELD_CH);    /* peak balance */
  w->a13 = cos_g / (2.0f * FIELD_CH);
  w->a24 = sin_g / (2.0f * FIELD_CH);
  w->w13 = wv - dw;
  w->w24 = wv + dw;
}

float field_tilt_max_gain(float sin_g, float cos_g)
{
  FieldTiltWeights w;
  float p13, p24;

  field_tilt_weights(sin_g, cos_g, &w);
  p13 = w.a13 * w.a13 + w.w13 * w.w13;
  p24 = w.a24 * w.a24 + w.w24 * w.w24;
  return 1.0f / sqrtf((p13 > p24) ? p13 : p24);
}

float field_tilt_shared_scale(float ah, float av, float sin_g, float cos_g,
                              float umax)
{
  FieldTiltWeights w;
  float p13, p24, pmax;

  field_tilt_weights(sin_g, cos_g, &w);
  /* u = a·Ah cosθ ± w·Av sinθ peaks at sqrt((a·Ah)² + (w·Av)²). */
  p13 = sqrtf((w.a13 * ah) * (w.a13 * ah) + (w.w13 * av) * (w.w13 * av));
  p24 = sqrtf((w.a24 * ah) * (w.a24 * ah) + (w.w24 * av) * (w.w24 * av));
  pmax = (p13 > p24) ? p13 : p24;

  if (!(umax > 0.0f) || !(pmax > umax)) return 1.0f;
  return umax / pmax;
}

void field_tilt_synthesize(float H, float V, float sin_g, float cos_g,
                           float raw[4])
{
  FieldTiltWeights w;

  if (!raw) return;
  field_tilt_weights(sin_g, cos_g, &w);
  raw[0] = -w.a13 * H + w.w13 * V; /* M1 */
  raw[1] =  w.a24 * H + w.w24 * V; /* M2 */
  raw[2] = -w.a13 * H - w.w13 * V; /* M3 */
  raw[3] =  w.a24 * H - w.w24 * V; /* M4 */
}

void field_tilt_quantize(const float raw[4], int16_t cmd[4], int16_t umax)
{
  int i;
  float u = (float)umax;

  if (!raw || !cmd) return;
  if (umax < 0) umax = (int16_t)(-umax);
  u = (float)umax;
  for (i = 0; i < 4; i++) {
    float v = raw[i];
    int16_t q;
    if (v > u) {
      q = umax;
    } else if (v < -u) {
      q = (int16_t)(-umax);
    } else if (v >= 0.0f) {
      q = (int16_t)(v + 0.5f);
    } else {
      q = (int16_t)(v - 0.5f);
    }
    cmd[i] = q;
  }
}

void field_tilt_forward(const float u[4], float *bx, float *by, float *bz)
{
  float x, y, z;

  if (!u) return;
  x = -FIELD_CH * (u[1] + u[3]);
  y =  FIELD_CV * (u[0] + u[1] - u[2] - u[3]);
  z = -FIELD_CH * (u[0] + u[2]);
  if (bx) *bx = x;
  if (by) *by = y;
  if (bz) *bz = z;
}

void field_tilt_coil_axis(uint8_t coil, float axis[3])
{
  if (!axis) return;
  switch (coil) {
    case 0:
      axis[0] = 0.0f;
      axis[1] = FIELD_CV;
      axis[2] = -FIELD_CH;
      break;
    case 1:
      axis[0] = -FIELD_CH;
      axis[1] = FIELD_CV;
      axis[2] = 0.0f;
      break;
    case 2:
      axis[0] = 0.0f;
      axis[1] = -FIELD_CV;
      axis[2] = -FIELD_CH;
      break;
    default:
      axis[0] = -FIELD_CH;
      axis[1] = -FIELD_CV;
      axis[2] = 0.0f;
      break;
  }
}

void field_tilt_reconstruct_center(const float b_lab[4][3], const float k[4],
                                   const uint8_t present[4],
                                   float *bx, float *by, float *bz)
{
  float u[4];
  int i;

  u[0] = u[1] = u[2] = u[3] = 0.0f;
  if (b_lab) {
    for (i = 0; i < 4; i++) {
      float ax[3], ki, dot;
      if (present && !present[i]) continue;
      field_tilt_coil_axis((uint8_t)i, ax);
      ki = (k && k[i] != 0.0f) ? k[i] : 1.0f;
      dot = b_lab[i][0] * ax[0] + b_lab[i][1] * ax[1] + b_lab[i][2] * ax[2];
      u[i] = dot / ki;
    }
  }
  field_tilt_forward(u, bx, by, bz);
}

void field_tilt_sensor_basis(uint8_t coil, int8_t z_sign,
                             float ex[3], float ey[3], float ez[3])
{
  float a[3], s, inv_n;
  int i;

  if (!ex || !ey || !ez) return;
  field_tilt_coil_axis(coil, a);
  s = (z_sign < 0) ? -1.0f : 1.0f;
  for (i = 0; i < 3; i++) ez[i] = s * a[i];
  /* x = +Y minus its axial part; |…| = sqrt(1 − a_y²) = c_h for every coil. */
  inv_n = 1.0f / FIELD_CH;
  ex[0] = -a[1] * a[0] * inv_n;
  ex[1] = (1.0f - a[1] * a[1]) * inv_n;
  ex[2] = -a[1] * a[2] * inv_n;
  /* y = z × x keeps (x, y, z) right-handed. */
  ey[0] = ez[1] * ex[2] - ez[2] * ex[1];
  ey[1] = ez[2] * ex[0] - ez[0] * ex[2];
  ey[2] = ez[0] * ex[1] - ez[1] * ex[0];
}

void field_tilt_sensor_to_lab(uint8_t coil, int8_t z_sign,
                              const float b_s[3], float b_lab[3])
{
  float ex[3], ey[3], ez[3];
  int i;

  if (!b_s || !b_lab) return;
  field_tilt_sensor_basis(coil, z_sign, ex, ey, ez);
  for (i = 0; i < 3; i++) {
    b_lab[i] = ex[i] * b_s[0] + ey[i] * b_s[1] + ez[i] * b_s[2];
  }
}

void field_tilt_center_from_sensors(const float b_s[4][3],
                                    const int8_t z_sign[4], const float k[4],
                                    const uint8_t present[4],
                                    float *bx, float *by, float *bz)
{
  float b_lab[4][3];
  int i;

  for (i = 0; i < 4; i++) {
    b_lab[i][0] = b_lab[i][1] = b_lab[i][2] = 0.0f;
    if (!b_s || (present && !present[i])) continue;
    field_tilt_sensor_to_lab((uint8_t)i, z_sign ? z_sign[i] : 1, b_s[i],
                             b_lab[i]);
  }
  /* Projecting the lab vector on â_i picks out exactly z_sign_i·Bz_i. */
  field_tilt_reconstruct_center(b_lab, k, present, bx, by, bz);
}

void field_tilt_commands(float ah, float av, float sin_th, float cos_th,
                         float sin_g, float cos_g, float umax,
                         float raw[4], float *scale_out)
{
  float s = field_tilt_shared_scale(ah, av, sin_g, cos_g, umax);
  float H = (ah * s) * cos_th;
  float V = (av * s) * sin_th;

  field_tilt_synthesize(H, V, sin_g, cos_g, raw);
  if (scale_out) *scale_out = s;
}
