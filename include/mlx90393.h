/**
 * @file mlx90393.h
 * @brief MLX90393 3-axis magnetometer on a dedicated I2C bus (not PCA9685).
 */
#ifndef MLX90393_H
#define MLX90393_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MLX90393_ADDR_MIN  0x0Cu
#define MLX90393_ADDR_MAX  0x1Bu

typedef struct {
  int16_t raw_x, raw_y, raw_z;
  float   ut_x, ut_y, ut_z;
  uint8_t status;
  uint8_t sat;
} MlxSample;

typedef struct {
  uint8_t  bus;      /* I2C_BUS_MLX */
  uint8_t  addr;
  uint8_t  present;
  uint8_t  gain;
  uint8_t  res;
  uint8_t  filt;
  uint8_t  osr;
  uint8_t  last_status;
  uint32_t conv_ms;
  uint32_t sm_tick;
} Mlx90393;

uint8_t MLX90393_Scan(uint8_t bus, uint8_t *found, uint8_t max);
uint8_t MLX90393_Init(Mlx90393 *dev, uint8_t bus, uint8_t addr);
uint8_t MLX90393_StartMeasure(Mlx90393 *dev);
uint8_t MLX90393_ReadIfReady(Mlx90393 *dev, MlxSample *out);
uint8_t MLX90393_ReadBlocking(Mlx90393 *dev, MlxSample *out);
void    MLX90393_Poll(Mlx90393 *dev, MlxSample *out, uint8_t *fresh);

#ifdef __cplusplus
}
#endif

#endif /* MLX90393_H */
