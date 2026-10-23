#include "pca9685.h"
#include "coil_output.h"
#include "field_engine.h"
#include "i2c_bus.h"
#include "platform_lock.h"
#include "config.h"

#include <string.h>

volatile uint8_t g_pca_dirty;
uint8_t g_pca_ok;

static uint8_t pca_write(uint8_t reg, const uint8_t *data, uint16_t n)
{
  uint8_t buf[64];
  if (n + 1U > sizeof(buf)) return 0;
  buf[0] = reg;
  if (n && data) memcpy(buf + 1, data, n);
  return i2c_tx(I2C_BUS_PCA, (uint8_t)PCA9685_I2C_ADDR, buf, (uint16_t)(n + 1U));
}

static uint8_t pca_write_u8(uint8_t reg, uint8_t val)
{
  return pca_write(reg, &val, 1);
}

static uint16_t pca_duty12(uint8_t duty)
{
  if (duty == 0) return 0;
  if (duty >= 255) return 4095;
  return (uint16_t)(((uint32_t)duty * 4095U + 127U) / 255U);
}

static void pca_pack_chan(uint8_t *out, uint8_t duty)
{
  uint16_t off = pca_duty12(duty);
  out[0] = 0;
  out[1] = 0;
  if (off == 0) {
    out[2] = 0;
    out[3] = 0x10;
  } else {
    out[2] = (uint8_t)(off & 0xFFU);
    out[3] = (uint8_t)((off >> 8) & 0x0FU);
  }
}

void PCA9685_Flush(void)
{
  uint8_t r[NUM_COILS], l[NUM_COILS];
  uint8_t payload[PCA_NCHAN * 4];
  uint8_t rot;
  FieldProgram prog;
  int i;

  if (!g_pca_ok || !g_pca_dirty) return;

  field_irq_lock();
  memcpy(r, g_pwm_r, sizeof(r));
  memcpy(l, g_pwm_l, sizeof(l));
  rot = g_rotate_active;
  prog = g_field_prog;
  g_pca_dirty = 0;
  field_irq_unlock();

  if (rot && prog == FIELD_ROTATE_Y) {
    r[2] = r[0];
    l[2] = l[0];
    r[3] = r[1];
    l[3] = l[1];
  }

  for (i = 0; i < NUM_COILS; i++) {
    pca_pack_chan(&payload[COIL_HW[i].pca_rpwm * 4], r[i]);
    pca_pack_chan(&payload[COIL_HW[i].pca_lpwm * 4], l[i]);
  }
  if (!pca_write(0x06, payload, sizeof(payload))) {
    g_pca_dirty = 1;
  }
}

void PCA9685_Init(void)
{
  uint32_t freq = (uint32_t)PWM_TARGET_HZ;
  uint32_t prescale;
  uint8_t zeros[PCA_NCHAN * 4];
  int i;

  g_pca_ok = 0;
  g_pca_dirty = 0;
  memset(g_pwm_r, 0, sizeof(g_pwm_r));
  memset(g_pwm_l, 0, sizeof(g_pwm_l));

  if (!i2c_ready(I2C_BUS_PCA, (uint8_t)PCA9685_I2C_ADDR)) return;

  if (!pca_write_u8(0x00, 0x10)) return;
  if (freq < 24U) freq = 24U;
  if (freq > 1526U) freq = 1526U;
  prescale = (PCA9685_OSC_HZ + 2048UL * freq) / (4096UL * freq);
  if (prescale > 0U) prescale -= 1U;
  if (prescale < 3U) prescale = 3U;
  if (prescale > 255U) prescale = 255U;
  if (!pca_write_u8(0xFE, (uint8_t)prescale)) return;
  if (!pca_write_u8(0x01, 0x04)) return;
  i2c_delay_ms(1);
  if (!pca_write_u8(0x00, 0xA1)) return;
  i2c_delay_ms(1);

  for (i = 0; i < PCA_NCHAN; i++) pca_pack_chan(&zeros[i * 4], 0);
  if (!pca_write(0x06, zeros, sizeof(zeros))) return;
  g_pca_ok = 1;
}
