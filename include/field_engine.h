#pragma once

#include "coil_types.h"
#include "field_tilt.h"
#include "field_fb.h"

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint8_t g_rotate_active;
extern float g_rotate_freq_hz;
extern uint8_t g_rotate_amp;
extern volatile uint8_t g_rotate_amp_live;
extern RotateDir g_rotate_dir;
extern RotateDir g_rotate_dir_want;
extern FieldProgram g_field_prog;
extern FieldProgram g_field_prog_want;
extern uint8_t g_revolve_h_pct;
extern uint8_t g_revolve_v_pct;
extern float g_tilt_gamma_want;
extern volatile float g_tilt_gamma_live;
extern volatile float g_tilt_sin_g;
extern volatile float g_tilt_cos_g;
extern volatile float g_tilt_scale;
extern volatile uint8_t g_rotate_stop_pending;
extern volatile uint32_t g_phase;
extern volatile uint32_t g_phase_inc;
extern volatile uint8_t g_revolve_phase_hold;
extern volatile uint8_t g_revolve_env_peak;
extern volatile uint8_t g_revolve_park;
extern volatile uint8_t g_revolve_corner;
extern volatile uint8_t g_revolve_travel;
extern volatile uint8_t g_aim_hold;     /* 1: slew phase to g_aim_elec, do not spin */
extern volatile uint32_t g_aim_elec;    /* electrical phase, CW convention (CCW negates) */
extern volatile uint8_t g_fb_apply;     /* 1: sample tick uses the MLX-corrected drive */

const char *rotate_dir_str(RotateDir d);
const char *field_program_str(FieldProgram p);
const char *field_axis_str(FieldProgram p);
uint8_t field_is_revolve(FieldProgram p);
uint8_t field_is_tilt(FieldProgram p);
uint8_t clamp_u8_pct(int v);
float Tilt_PreviewScale(uint8_t amp);
float Revolve_PreviewGain(void);

/* MLX feedback hooks (see field_fb.h). */
void    Field_IdealCoeffs(float alpha[4], float beta[4]); /* live program, uncorrected */
float   Field_SynTheta(uint32_t phase);                   /* synthesis phase, rad */
uint8_t Field_FbSteady(void);                             /* no ramp/hold/park */
void    Field_FbSetK(const FbCplx k[4], RotateDir dir);   /* k in that dir's θ */

void Rotate_RecalcPhaseInc(void);
void Rotate_Stop(void);
void Rotate_RequestStop(void);
void Rotate_SetDir(RotateDir d);
void Field_SetProgram(FieldProgram p);
void Field_ApplyOptionalFreqAmp(int argc, char **argv, int start);
void Field_Start(FieldProgram p);
void Tilt_CommitGamma(float deg);
void Tilt_SetGamma(float deg);
void Revolve_ClearPark(void);
void Revolve_GotoCorner(uint8_t q);
/* Stick aim: slew to electrical degrees (0 = +Bh / stick right, 90 = +Bv / stick up). */
void Field_AimDeg(float deg);
void Field_AimRun(void); /* release aim; rotation continues from the current phase */
void Rotate_StopIfDcConflict(uint8_t idx);
void Rotate_RampUpdate(void);
void Field_SampleTick(void);
void Field_EngineInit(void);

#ifdef __cplusplus
}
#endif
