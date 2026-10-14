/**
 * @file mlx90393.h
 * @brief MLX90393 3-axis magnetometer on a dedicated I2C bus (not PCA9685).
 * Firmware polls up to four chips (one behind each coil).
 *
 * Burst/single-measurement XYZ in mT. Axis remap is applied by the caller.
 */
#ifndef MLX90393_H
#define MLX90393_H

#include "stm32h7xx_hal.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MLX90393_ADDR_MIN  0x0Cu
#define MLX90393_ADDR_MAX  0x1Bu

typedef struct {
  int16_t raw_x, raw_y, raw_z;
  float   ut_x, ut_y, ut_z; /* chip frame, microtesla */
  uint8_t status;
  uint8_t sat;              /* 1 = error flag or near-rail counts */
} MlxSample;

typedef struct {
  I2C_HandleTypeDef *i2c;
  uint8_t            addr;     /* 7-bit */
  uint8_t            present;
  uint8_t            gain;     /* 0..7  (7 = 1x, widest analog) */
  uint8_t            res;      /* 0..3  (3 = 19-bit, widest range) */
  uint8_t            filt;     /* 0..7 */
  uint8_t            osr;      /* 0..3 */
  uint8_t            last_status;
  uint32_t           conv_ms;
  uint32_t           sm_tick;  /* HAL tick when SM was issued; 0 = idle */
} Mlx90393;

uint8_t MLX90393_Scan(I2C_HandleTypeDef *hi2c, uint8_t *found, uint8_t max);
uint8_t MLX90393_Init(Mlx90393 *dev, I2C_HandleTypeDef *hi2c, uint8_t addr);
uint8_t MLX90393_StartMeasure(Mlx90393 *dev);
uint8_t MLX90393_ReadIfReady(Mlx90393 *dev, MlxSample *out);
uint8_t MLX90393_ReadBlocking(Mlx90393 *dev, MlxSample *out);
void    MLX90393_Poll(Mlx90393 *dev, MlxSample *out, uint8_t *fresh);

#ifdef __cplusplus
}
#endif

#endif /* MLX90393_H */
