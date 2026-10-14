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
  } else if (gamma_deg >= FIELD_TILT_GAMMA_MAX) {
    sg = 1.0f;
    cg = 0.0f;
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

float field_tilt_shared_scale(float ah, float av, float sin_g, float cos_g,
                              float umax)
{
  float inv_h = 1.0f / (2.0f * FIELD_CH);
  float inv_v = 1.0f / (4.0f * FIELD_CV);
  float p13 = sqrtf((ah * cos_g * inv_h) * (ah * cos_g * inv_h) +
                    (av * inv_v) * (av * inv_v));
  float p24 = sqrtf((ah * sin_g * inv_h) * (ah * sin_g * inv_h) +
                    (av * inv_v) * (av * inv_v));
  float pmax = (p13 > p24) ? p13 : p24;

  if (!(umax > 0.0f) || !(pmax > umax)) return 1.0f;
  return umax / pmax;
}

void field_tilt_synthesize(float H, float V, float sin_g, float cos_g,
                           float raw[4])
{
  float inv_h;
  float inv_v;

  if (!raw) return;
  inv_h = 1.0f / (2.0f * FIELD_CH);
  inv_v = 1.0f / (4.0f * FIELD_CV);
  raw[0] = -H * cos_g * inv_h + V * inv_v; /* M1 */
  raw[1] =  H * sin_g * inv_h + V * inv_v; /* M2 */
  raw[2] = -H * cos_g * inv_h - V * inv_v; /* M3 */
  raw[3] =  H * sin_g * inv_h - V * inv_v; /* M4 */
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
