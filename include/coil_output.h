#pragma once

#include "coil_types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const CoilHw COIL_HW[NUM_COILS];
extern CoilState coils[NUM_COILS];
extern uint8_t g_pwm_r[NUM_COILS];
extern uint8_t g_pwm_l[NUM_COILS];
extern volatile int16_t g_signed_d[NUM_COILS];
extern uint8_t g_contour_active;

void Coil_GpioInit(void);
void Coil_SetPwm(uint8_t idx, uint8_t rpwm, uint8_t lpwm);
void Coil_ApplyOutputs(uint8_t idx);
void Coil_ApplySignedDuty(uint8_t idx, int16_t signed_duty);
void Coil_ApplyPairedSignedDuty(uint8_t a, uint8_t b, int16_t signed_duty);
void Coil_AllOff(void);
void Coil_SetEnabled(uint8_t idx, uint8_t on);
void Coil_SetMagnitude(uint8_t idx, uint8_t mag);
void Coil_SetDir(uint8_t idx, CoilDir d);

#ifdef __cplusplus
}
#endif
