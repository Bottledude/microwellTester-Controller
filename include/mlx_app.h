#pragma once

#include "mlx90393.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Mlx90393 g_mlx_coil[NUM_COILS];
extern uint8_t g_mlx_bind[NUM_COILS];
extern uint8_t g_mlx_src[NUM_COILS][3];
extern int8_t g_mlx_sgn[NUM_COILS][3];
extern float g_mlx_b0_mt[NUM_COILS][3];
extern uint8_t g_mlx_b0_ok[NUM_COILS];
extern int8_t g_mlx_zsign[NUM_COILS];     /* chip +z along (+1) / against (−1) â_i */
extern float g_mlx_s_mt[NUM_COILS][3];    /* chip frame: x up, y left, z axial */
extern float g_mlx_lab_mt[NUM_COILS][3];
extern float g_mlx_babs_mt[NUM_COILS];
extern float g_mlx_k[NUM_COILS];
extern uint8_t g_mlx_sat[NUM_COILS];
extern float g_mlx_center_mt[3];
extern float g_mlx_center_babs;
extern uint8_t g_mlx_center_sat;
extern uint8_t g_mlx_n_present;
extern uint8_t g_mlx_en;

void Mlx_ReconstructCenter(void);
void Mlx_PrintStatus(void);
void mlx_try_init(void);
void Mlx_PollAll(void);
uint8_t mlx_take_offset(void);
uint8_t mlx_parse_axis_tok(const char *t, uint8_t *src, int8_t *sgn);

#ifdef __cplusplus
}
#endif
