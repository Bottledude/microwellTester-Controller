/**
 * @file field_fb.h
 * @brief MLX90393 closed-loop per-coil correction (no HAL). Host-testable.
 *
 * Each coil is driven with a sinusoid in the synthesis phase θ:
 *   u_i(θ) = α_i cosθ + β_i sinθ
 * Phasor convention: x(θ) = Re{X e^{jθ}}, so U_i = α_i − jβ_i.
 *
 * Chip i's axial reading m_i (zsign·Bz/k, mT) is coil i's contribution at the
 * centre (see field_tilt_center_from_sensors). Over about one revolution it
 * is least-squares fitted as m_i ≈ p cosθ + q sinθ + r, giving M_i = p − jq
 * (r soaks up any residual DC offset).
 *
 *   Drive actually applied   D_i = K_i·U_i        (K_i = correction, starts at 1)
 *   Per-coil gain            G_i = M_i / D_i      (mT per duty, with phase lag)
 *   Best common gain         Ĝ_fit = Σ M_i U_i* / Σ |U_i|²
 *   Target gain              Ĝ = Ĝ_fit, magnitude capped so no coil needs more
 *                            than umax: |Ĝ| ≤ |G_i|·umax/|U_i| for every i
 *   New correction           K_i ← K_i + μ (Ĝ/G_i − K_i)
 *
 * Applying K_i scales coil i by |K_i| and advances its phase by arg K_i:
 *   u_i(θ) = α_i (kr cosθ − ki sinθ) + β_i (kr sinθ + ki cosθ)
 * At the fixed point every coil gives Ĝ per duty, so the reconstructed field
 * is Ĝ × the commanded (geometric) shape: the same circle, just in mT.
 *
 * A delay common to all four chips (MLX conversion, poll latency) only shifts
 * arg Ĝ and cancels. The fit works in θ, not time, so it still works when
 * 50 Hz sampling is coarse relative to the rotation, as long as the sampled
 * phases spread around the circle (checked by the normal-matrix determinant).
 *
 * Error reported (field level, what Teleplot shows):
 *   err = |Σ (M_i − Ĝ_fit U_i) â_i| / |Σ Ĝ_fit U_i â_i|
 */
#ifndef FIELD_FB_H
#define FIELD_FB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FB_NCOIL 4

typedef struct {
  float re;
  float im;
} FbCplx;

typedef struct {
  float    mu;          /* update step 0..1 */
  float    kmin;        /* |K| bounds (safety) */
  float    kmax;
  float    psi_max_rad; /* refuse a phase correction larger than this */
  float    umax;        /* per-coil duty limit */
  float    min_drive;   /* skip coils whose |U_i| is below this (duty) */
  uint32_t min_samples; /* samples per update window */
} FbConfig;

typedef struct {
  /* LSQ accumulators over the current window */
  uint32_t n;
  float scc, sss, scs, sc, ss;
  float smc[FB_NCOIL], sms[FB_NCOIL], sm[FB_NCOIL];
  /* learned / reported */
  FbCplx   k[FB_NCOIL];  /* correction, synthesis-phase convention */
  FbCplx   g[FB_NCOIL];  /* last per-coil gain estimate G_i */
  FbCplx   gfit;         /* last Ĝ_fit (mT per duty) */
  float    err;          /* last field error (fraction) */
  uint8_t  ok;           /* gfit / err valid */
  uint8_t  fault;        /* bit i: coil i correction refused (phase/gain out of range) */
  uint32_t updates;
} FieldFb;

void   field_fb_default_config(FbConfig *cfg);
/* K = 1, clear estimate and window. */
void   field_fb_init(FieldFb *fb);
/* Drop the current window (drive or config changed). */
void   field_fb_clear_window(FieldFb *fb);
/* Swap synthesis direction (CW ↔ CCW): a time lag flips sign in θ. */
void   field_fb_conjugate(FieldFb *fb);
void   field_fb_add(FieldFb *fb, float theta, const float m[FB_NCOIL]);

/*
 * Fit the window if it has enough samples and good phase coverage.
 * alpha/beta: ideal (uncorrected) coefficients of the window's drive.
 * adapt = 0 only estimates (gfit, err, g); adapt = 1 also updates k.
 * Returns 1 when the window was used (and cleared), 0 otherwise.
 */
int    field_fb_update(FieldFb *fb, const FbConfig *cfg,
                       const float alpha[FB_NCOIL], const float beta[FB_NCOIL],
                       uint8_t adapt);

/* Corrected duty for one coil at (cosθ, sinθ). */
float  field_fb_coil(FbCplx k, float alpha, float beta, float c, float s);

/* Expected centre field (mT) at θ from Ĝ_fit and the ideal coefficients. */
void   field_fb_expected(const FieldFb *fb, const float alpha[FB_NCOIL],
                         const float beta[FB_NCOIL], float theta, float out[3]);

float  fb_cabs(FbCplx a);
float  fb_carg(FbCplx a);

#ifdef __cplusplus
}
#endif

#endif /* FIELD_FB_H */
