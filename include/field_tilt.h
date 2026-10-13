/**
 * @file field_tilt.h
 * @brief Pure tilted-axis revolve synthesis (no HAL). Host-testable.
 *
 * Rotation axis is fixed in the XZ plane:
 *   n = (cos γ, 0, sin γ),  γ from +X toward +Z (degrees at the UI).
 * The field rotates in the plane spanned by
 *   h = (−sin γ, 0, cos γ)  and  v = (0, 1, 0).
 * Increasing electrical phase θ has angular-velocity direction −n
 * (h × v = −n). Firmware reuses existing rot_dir: CW = +θ, CCW = −θ,
 * applied once in the phase engine, not again here.
 *
 * Existing +Y forward map (do not invert):
 *   Bx = −c (u2 + u4)
 *   By =  c (u1 + u2 − u3 − u4)
 *   Bz = −c (u1 + u3)
 *   c  = √2 / 2
 */
#ifndef FIELD_TILT_H
#define FIELD_TILT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIELD_TILT_C            0.7071067811865476f
#define FIELD_TILT_INV_SQRT2    FIELD_TILT_C
#define FIELD_TILT_UMAX         255.0f
#define FIELD_TILT_GAMMA_MIN    0.0f
#define FIELD_TILT_GAMMA_MAX    90.0f

typedef struct {
  float deg;
  float sin_g;
  float cos_g;
} FieldTiltCoeffs;

/* Parse UI degrees. Returns 1 and writes *out only on success (0..90 inclusive). */
int field_tilt_parse_gamma(const char *s, float *out);

/* Snap 0°/90° to exact (0,1)/(1,0). Otherwise sinf/cosf of radians. */
void field_tilt_gamma_sincos(float gamma_deg, float *sin_g, float *cos_g);

/* Fill a local struct then assign — ISR-safe when the caller copies under irq lock. */
void field_tilt_coeffs_from_deg(float gamma_deg, FieldTiltCoeffs *out);

/* Shared scale s so no coil exceeds umax. s = 1 if Pmax <= umax. */
float field_tilt_shared_scale(float ah, float av, float sin_g, float cos_g,
                              float umax);

/* Unquantized M1..M4 from lab H/V at this sample (already scaled if needed). */
void field_tilt_synthesize(float H, float V, float sin_g, float cos_g,
                           float raw[4]);

/* Round-to-nearest, ties away from 0, then clamp ±umax (matches clamp_i16_pwm). */
void field_tilt_quantize(const float raw[4], int16_t cmd[4], int16_t umax);

/* Existing +Y forward map from signed coil commands. */
void field_tilt_forward(const float u[4], float *bx, float *by, float *bz);

/*
 * H = (Ah*s) cosθ, V = (Av*s) sinθ, then synthesize.
 * sin_th / cos_th are the electrical-phase pair (LUT or exact).
 */
void field_tilt_commands(float ah, float av, float sin_th, float cos_th,
                         float sin_g, float cos_g, float umax,
                         float raw[4], float *scale_out);

#ifdef __cplusplus
}
#endif

#endif /* FIELD_TILT_H */
