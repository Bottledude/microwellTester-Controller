/**
 * @file display_ui.h
 * @brief Onboard TFT live coil status (per-driver rows).
 */
#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t enabled;
  uint8_t magnitude;
  int8_t  dir;       /* +1 forward, -1 reverse */
  float   i_meas_a;
  float   i_limit_a;
} DisplayCoilSnap;

typedef struct {
  uint8_t present;
  uint8_t sat;
  float   bx_mt, by_mt, bz_mt, babs_mt;
} DisplayMlxSnap;

void DisplayUI_Init(void);
void DisplayUI_Update(const char *mode, uint8_t contour,
                      uint8_t rotate, float freq_hz, uint8_t amp,
                      const char *field_tag, /* "rotY", "revX", or "revZ" */
                      const DisplayCoilSnap *coils, int n,
                      const DisplayMlxSnap *mlx);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_UI_H */
