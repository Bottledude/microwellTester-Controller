#pragma once

#include "field_fb.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t  g_fb_en;   /* toggle: 1 = correct the PWM from the MLX chips */
extern FieldFb  g_fb;
extern FbConfig g_fb_cfg;

/* on = 1 needs all four chips. Both directions restart from K = 1. */
uint8_t Fb_SetEnabled(uint8_t on);
void    Fb_Reset(void);
/* One full chip set from a single trigger (flush task, I2C1). */
void    Fb_OnMlxSet(uint32_t phase, uint8_t steady_at_trigger,
                    const float m[4], uint8_t sat);
/* Expected centre B (mT) at the last chip set's phase. 0 if no estimate yet. */
uint8_t Fb_Expected(float out[3]);
void    Fb_PrintStatus(void);

#ifdef __cplusplus
}
#endif
