#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint8_t g_pca_dirty;
extern uint8_t g_pca_ok;

void PCA9685_Init(void);
void PCA9685_Flush(void);

#ifdef __cplusplus
}
#endif
