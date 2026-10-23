/**
 * @file field_tilt.h
 * @brief Pure tilted-axis revolve synthesis (no HAL). Host-testable.
 *
 * Rotation axis is fixed in the XZ plane:
 *   n = (cos γ, 0, sin γ),  γ from +X toward +Z, 0..180° (degrees at the UI).
 *   0..180 covers every axis in the plane; γ+180 is the same axis with the
 *   rotation reversed, which rot_dir already covers.
 * The field rotates in the plane spanned by
 *   h = (−sin γ, 0, cos γ)  and  v = (0, 1, 0).
 * Increasing electrical phase θ has angular-velocity direction −n
 * (h × v = −n). Firmware reuses existing rot_dir: CW = +θ, CCW = −θ,
 * applied once in the phase engine, not again here.
 *
 * Mechanical coil tilt is 30° from horizontal:
 *   c_h = cos 30° = √3/2,  c_v = sin 30° = 1/2
 * Forward map (signed PWM as current/B proxy):
 *   Bx = −c_h (u2 + u4)
 *   By =  c_v (u1 + u2 − u3 − u4)
 *   Bz = −c_h (u1 + u3)
 *
 * Each coil at duty u gives c_v·u vertical and c_h·u horizontal (along its
 * own pair's horizontal axis). Within a pair the two horizontal parts add
 * when both coils have the same sign and the vertical parts cancel; with
 * opposite signs the vertical parts add and the horizontal parts cancel.
 *
 * Target in the tilted plane: B(θ) = H ĥ + V ŷ with H = Ah cosθ, V = Av sinθ.
 * Inverse = one linear law per coil, so every coil is a pure sinusoid:
 *   u1 = −a13·H + w13·V      u3 = −a13·H − w13·V     (M1/M3: H along Z, + V)
 *   u2 = +a24·H + w24·V      u4 = +a24·H − w24·V     (M2/M4: H along X, + V)
 * Horizontal weights (fixed by geometry):
 *   a13 = cosγ / (2 c_h)     a24 = sinγ / (2 c_h)
 * Vertical weights (shared by both pairs, w13 + w24 = 1 / (2 c_v)):
 *   w13 = 1/(4 c_v) − cos2γ · c_v / (4 c_h²)    = 1/2 − cos2γ/6   at 30°
 *   w24 = 1/(4 c_v) + cos2γ · c_v / (4 c_h²)    = 1/2 + cos2γ/6
 * The split is chosen so a13² + w13² = a24² + w24²: both pairs need the same
 * peak duty, so at the max circle all four coils reach ±umax together.
 *   γ = 0   (revolve X): M1/M3 carry H and 1/3 of V; M2/M4 carry 2/3 of V
 *   γ = 90  (revolve Z): M2/M4 carry H and 1/3 of V; M1/M3 carry 2/3 of V
 *   γ = 45: both pairs carry half of H and half of V
 * Peak duty per unit radius is sqrt(a13² + w13²) = sqrt(5/12 + cos²2γ/36),
 * so the largest circle with H = V is
 *   R_max = umax · G,  G = 6 / sqrt(15 + cos²2γ)
 *   (G = 1.5 at γ = 0/90/180, 1.549 at γ = 45/135)
 * A larger circle (e.g. the 2 c_h·umax = √3·umax that the horizontal pair
 * alone could reach) would need M1/M3 above ±umax mid-quadrant, because
 * they must carry H and V at the same time; with sinusoidal drive the
 * horizontal pair cannot spend its full duty on H and still help V.
 */
#ifndef FIELD_TILT_H
#define FIELD_TILT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIELD_CH            0.8660254037844386f /* cos 30° */
#define FIELD_CV            0.5f                /* sin 30° */
#define FIELD_INV_SQRT2     0.7071067811865476f
#define FIELD_TILT_UMAX     255.0f
#define FIELD_TILT_GAMMA_MIN    0.0f
#define FIELD_TILT_GAMMA_MAX    180.0f

typedef struct {
  float deg;
  float sin_g;
  float cos_g;
} FieldTiltCoeffs;

/* Per-coil weights of the inverse (see the header comment). */
typedef struct {
  float a13; /* M1/M3 horizontal: u1,u3 ∋ −a13·H */
  float a24; /* M2/M4 horizontal: u2,u4 ∋ +a24·H */
  float w13; /* M1/M3 vertical:   u1 ∋ +w13·V, u3 ∋ −w13·V */
  float w24; /* M2/M4 vertical:   u2 ∋ +w24·V, u4 ∋ −w24·V */
} FieldTiltWeights;

/* Parse UI degrees. Returns 1 and writes *out only on success (0..180 inclusive). */
int field_tilt_parse_gamma(const char *s, float *out);

/* Snap 0°/90°/180° to exact (0,1)/(1,0)/(0,−1). Otherwise sinf/cosf of radians. */
void field_tilt_gamma_sincos(float gamma_deg, float *sin_g, float *cos_g);

/* Fill a local struct then assign — ISR-safe when the caller copies under irq lock. */
void field_tilt_coeffs_from_deg(float gamma_deg, FieldTiltCoeffs *out);

/* a13/a24/w13/w24 for this plane. */
void field_tilt_weights(float sin_g, float cos_g, FieldTiltWeights *w);

/* G = R_max / umax for a circle (H = V) in this plane: 6 / sqrt(15 + cos²2γ). */
float field_tilt_max_gain(float sin_g, float cos_g);

/*
 * Shared scale s so the (ah, av) ellipse keeps every coil within ±umax.
 * Coil peak = sqrt((a·ah)² + (w·av)²) for its pair. s = 1 if it already fits.
 */
float field_tilt_shared_scale(float ah, float av, float sin_g, float cos_g,
                              float umax);

/* Unquantized M1..M4 from lab H/V at this sample (already scaled if needed). */
void field_tilt_synthesize(float H, float V, float sin_g, float cos_g,
                           float raw[4]);

/* Round-to-nearest, ties away from 0, then clamp ±umax (matches clamp_i16_pwm). */
void field_tilt_quantize(const float raw[4], int16_t cmd[4], int16_t umax);

/* 30° +Y forward map from signed coil commands. */
void field_tilt_forward(const float u[4], float *bx, float *by, float *bz);

/* Unit coil axis in lab (M1=0 … M4=3) from the forward-map columns. */
void field_tilt_coil_axis(uint8_t coil, float axis[3]);

/*
 * Reconstruct centre B: u_i = (B_i · â_i) / k_i (missing coils → 0), then forward.
 * b_lab[i][0..2] is lab Bx,By,Bz for the sensor behind coil i.
 */
void field_tilt_reconstruct_center(const float b_lab[4][3], const float k[4],
                                   const uint8_t present[4],
                                   float *bx, float *by, float *bz);

/*
 * Lab-frame unit axes of the MLX90393 chip mounted behind coil i:
 *   z: along the coil axis, z_sign=+1 → same way as â_i, −1 → opposite
 *   x: "up" = lab +Y with the axial part removed (tilts 30° with the coil)
 *   y: z × x, i.e. "left" when looking at the chip face (z toward you)
 */
void field_tilt_sensor_basis(uint8_t coil, int8_t z_sign,
                             float ex[3], float ey[3], float ez[3]);

/* Chip-frame reading (x up, y left, z axial) → lab Bx,By,Bz. */
void field_tilt_sensor_to_lab(uint8_t coil, int8_t z_sign,
                              const float b_s[3], float b_lab[3]);

/*
 * Centre B from the four behind-coil chips, each in its own frame.
 * The chip sits as far behind the coil as the centre sits in front, so it is
 * the mirror of the centre through the coil midplane. A coil's on-axis field
 * is even about that plane, so the chip's axial reading equals that coil's
 * contribution at the centre: u_i = z_sign_i·Bz_i / k_i (missing → 0), then
 * centre = Σ u_i â_i (the forward map). Chip x/y are not used: on axis the
 * coil's own transverse field is 0, so they only show misalignment/crosstalk.
 */
void field_tilt_center_from_sensors(const float b_s[4][3],
                                    const int8_t z_sign[4], const float k[4],
                                    const uint8_t present[4],
                                    float *bx, float *by, float *bz);

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
