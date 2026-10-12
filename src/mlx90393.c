/**
 * @file mlx90393.c
 * @brief I2C driver for MLX90393 (Melexis Triaxis). Own bus — never I2C1/PCA9685.
 *
 * Commands follow the datasheet / Adafruit protocol:
 *   EX 0x80, RT 0xF0, WR 0x60, RR 0x50, SM 0x30|zyxt, RM 0x40|zyxt
 * RM XYZ order is T,X,Y,Z with only enabled axes: X then Y then Z.
 */
#include "mlx90393.h"

#include <string.h>

#define CMD_SB   0x10u
#define CMD_SM   0x30u
#define CMD_RM   0x40u
#define CMD_RR   0x50u
#define CMD_WR   0x60u
#define CMD_EX   0x80u
#define CMD_RT   0xF0u
#define AXIS_XYZ 0x0Eu

#define REG_CONF1 0x00u
#define REG_CONF3 0x02u
#define HALLCONF  0x0Cu

#define GAIN_SHIFT 4u
#define ST_ERROR   0x10u

/* HALLCONF=0x0C: [gain 0..7][res 0..3][xy,z] µT/LSB (Adafruit table). */
static const float LSB_UT[8][4][2] = {
  {{0.751f, 1.210f}, {1.502f, 2.420f}, {3.004f, 4.840f}, {6.009f, 9.680f}},
  {{0.601f, 0.968f}, {1.202f, 1.936f}, {2.403f, 3.872f}, {4.840f, 7.744f}},
  {{0.451f, 0.726f}, {0.901f, 1.452f}, {1.803f, 2.904f}, {3.605f, 5.808f}},
  {{0.376f, 0.605f}, {0.751f, 1.210f}, {1.502f, 2.420f}, {3.004f, 4.840f}},
  {{0.300f, 0.484f}, {0.601f, 0.968f}, {1.202f, 1.936f}, {2.403f, 3.872f}},
  {{0.250f, 0.403f}, {0.501f, 0.807f}, {1.001f, 1.613f}, {2.003f, 3.227f}},
  {{0.200f, 0.323f}, {0.401f, 0.645f}, {0.801f, 1.291f}, {1.602f, 2.581f}},
  {{0.150f, 0.242f}, {0.300f, 0.484f}, {0.601f, 0.968f}, {1.202f, 1.936f}},
};

/* DIG_FILT × OSR conversion time (ms), datasheet 15.1.5. */
static const uint16_t TCONV_MS[8][4] = {
  {2, 2, 3, 6},
  {2, 3, 4, 7},
  {2, 3, 6, 10},
  {3, 5, 9, 17},
  {5, 8, 15, 29},
  {8, 14, 27, 53},
  {14, 27, 52, 103},
  {26, 51, 101, 201},
};

static uint16_t i2c_addr8(const Mlx90393 *dev)
{
  return (uint16_t)((uint16_t)dev->addr << 1);
}

static HAL_StatusTypeDef mlx_tx(Mlx90393 *dev, const uint8_t *buf, uint16_t n)
{
  return HAL_I2C_Master_Transmit(dev->i2c, i2c_addr8(dev), (uint8_t *)buf, n, 8);
}

static HAL_StatusTypeDef mlx_rx(Mlx90393 *dev, uint8_t *buf, uint16_t n)
{
  return HAL_I2C_Master_Receive(dev->i2c, i2c_addr8(dev), buf, n, 8);
}

static uint8_t mlx_cmd_status(Mlx90393 *dev, const uint8_t *tx, uint16_t ntx)
{
  uint8_t st = 0;
  if (mlx_tx(dev, tx, ntx) != HAL_OK) return 0x10u;
  if (mlx_rx(dev, &st, 1) != HAL_OK) return 0x10u;
  dev->last_status = st;
  return st;
}

static uint8_t mlx_write_reg(Mlx90393 *dev, uint8_t reg, uint16_t val)
{
  uint8_t buf[4];
  buf[0] = (uint8_t)CMD_WR;
  buf[1] = (uint8_t)(val >> 8);
  buf[2] = (uint8_t)(val & 0xFFu);
  buf[3] = (uint8_t)(reg << 2);
  return mlx_cmd_status(dev, buf, 4);
}

static int16_t unpack_axis(uint8_t res, const uint8_t *be)
{
  uint16_t u = (uint16_t)(((uint16_t)be[0] << 8) | be[1]);
  if (res == 3u) return (int16_t)((int32_t)u - 0x4000);
  if (res == 2u) return (int16_t)((int32_t)u - 0x8000);
  return (int16_t)u;
}

static void sample_from_rm(Mlx90393 *dev, const uint8_t *rm, MlxSample *out)
{
  uint8_t g = dev->gain > 7u ? 7u : dev->gain;
  uint8_t r = dev->res > 3u ? 3u : dev->res;
  float lsb_xy = LSB_UT[g][r][0];
  float lsb_z = LSB_UT[g][r][1];
  int16_t x, y, z;
  uint8_t sat = 0;

  memset(out, 0, sizeof(*out));
  out->status = rm[0];
  dev->last_status = rm[0];
  x = unpack_axis(dev->res, &rm[1]);
  y = unpack_axis(dev->res, &rm[3]);
  z = unpack_axis(dev->res, &rm[5]);
  out->raw_x = x;
  out->raw_y = y;
  out->raw_z = z;
  out->ut_x = (float)x * lsb_xy;
  out->ut_y = (float)y * lsb_xy;
  out->ut_z = (float)z * lsb_z;
  if (rm[0] & ST_ERROR) sat = 1;
  if (x > 30000 || x < -30000 || y > 30000 || y < -30000 ||
      z > 30000 || z < -30000) {
    sat = 1;
  }
  out->sat = sat;
}

uint8_t MLX90393_Scan(I2C_HandleTypeDef *hi2c, uint8_t *found, uint8_t max)
{
  uint8_t n = 0;
  if (!hi2c || !found || max == 0) return 0;
  for (uint8_t a = MLX90393_ADDR_MIN; a <= MLX90393_ADDR_MAX; a++) {
    if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)((uint16_t)a << 1), 2, 8) == HAL_OK) {
      found[n++] = a;
      if (n >= max) break;
    }
  }
  return n;
}

uint8_t MLX90393_Init(Mlx90393 *dev, I2C_HandleTypeDef *hi2c, uint8_t addr)
{
  uint8_t cmd;
  uint16_t conf3;

  if (!dev || !hi2c) return 0;
  memset(dev, 0, sizeof(*dev));
  dev->i2c = hi2c;
  dev->addr = addr;
  /* Widest practical range: analog 1x + 19-bit encoding (~±39 mT XY / ±63 mT Z). */
  dev->gain = 7u;
  dev->res = 3u;
  dev->filt = 2u;
  dev->osr = 1u;
  dev->conv_ms = (uint32_t)TCONV_MS[dev->filt][dev->osr] + 2u;

  if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)((uint16_t)addr << 1), 3, 12) != HAL_OK) {
    return 0;
  }

  cmd = (uint8_t)CMD_EX;
  (void)mlx_cmd_status(dev, &cmd, 1);
  HAL_Delay(2);
  cmd = (uint8_t)CMD_RT;
  (void)mlx_cmd_status(dev, &cmd, 1);
  HAL_Delay(3);

  if (mlx_write_reg(dev, REG_CONF1, (uint16_t)((dev->gain << GAIN_SHIFT) | HALLCONF)) & ST_ERROR) {
    return 0;
  }
  /* CONF3: OSR[1:0], FILT[4:2], RES_X[6:5], RES_Y[8:7], RES_Z[10:9] */
  conf3 = (uint16_t)(dev->osr & 3u);
  conf3 |= (uint16_t)((uint16_t)(dev->filt & 7u) << 2);
  conf3 |= (uint16_t)((uint16_t)(dev->res & 3u) << 5);
  conf3 |= (uint16_t)((uint16_t)(dev->res & 3u) << 7);
  conf3 |= (uint16_t)((uint16_t)(dev->res & 3u) << 9);
  if (mlx_write_reg(dev, REG_CONF3, conf3) & ST_ERROR) {
    return 0;
  }

  dev->present = 1;
  dev->sm_tick = 0;
  return 1;
}

uint8_t MLX90393_StartMeasure(Mlx90393 *dev)
{
  uint8_t cmd;
  if (!dev || !dev->present) return 0;
  cmd = (uint8_t)(CMD_SM | AXIS_XYZ);
  if (mlx_cmd_status(dev, &cmd, 1) & ST_ERROR) return 0;
  dev->sm_tick = HAL_GetTick();
  if (dev->sm_tick == 0u) dev->sm_tick = 1u;
  return 1;
}

uint8_t MLX90393_ReadIfReady(Mlx90393 *dev, MlxSample *out)
{
  uint8_t cmd;
  uint8_t rm[7];
  uint32_t now;

  if (!dev || !dev->present || !out || dev->sm_tick == 0u) return 0;
  now = HAL_GetTick();
  if ((now - dev->sm_tick) < dev->conv_ms) return 0;

  cmd = (uint8_t)(CMD_RM | AXIS_XYZ);
  if (mlx_tx(dev, &cmd, 1) != HAL_OK) {
    dev->sm_tick = 0;
    return 0;
  }
  if (mlx_rx(dev, rm, 7) != HAL_OK) {
    dev->sm_tick = 0;
    return 0;
  }
  sample_from_rm(dev, rm, out);
  dev->sm_tick = 0;
  return 1;
}

uint8_t MLX90393_ReadBlocking(Mlx90393 *dev, MlxSample *out)
{
  uint32_t t0;
  if (!MLX90393_StartMeasure(dev)) return 0;
  t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < (dev->conv_ms + 8u)) {
    if (MLX90393_ReadIfReady(dev, out)) return 1;
  }
  /* One last attempt after the wait window. */
  return MLX90393_ReadIfReady(dev, out);
}

void MLX90393_Poll(Mlx90393 *dev, MlxSample *out, uint8_t *fresh)
{
  if (fresh) *fresh = 0;
  if (!dev || !dev->present) return;
  if (dev->sm_tick) {
    if (MLX90393_ReadIfReady(dev, out)) {
      if (fresh) *fresh = 1;
      (void)MLX90393_StartMeasure(dev);
    }
    return;
  }
  (void)MLX90393_StartMeasure(dev);
}
