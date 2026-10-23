#pragma once

#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DIR_FWD = 0,
  DIR_REV = 1
} CoilDir;

typedef enum {
  ROT_CW = 0,
  ROT_CCW = 1
} RotateDir;

typedef enum {
  FIELD_ROTATE_Y = 0,
  FIELD_REVOLVE_X = 1,
  FIELD_REVOLVE_Z = 2,
  FIELD_REVOLVE_TILT = 3
} FieldProgram;

typedef struct {
  uint8_t pca_rpwm;
  uint8_t pca_lpwm;
  uint8_t en_pin;
} CoilHw;

typedef struct {
  uint8_t  enabled;
  uint8_t  magnitude;
  CoilDir  dir;
  uint16_t duty_applied;
} CoilState;

#ifdef __cplusplus
}
#endif
