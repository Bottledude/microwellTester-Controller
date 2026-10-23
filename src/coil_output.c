#include "coil_output.h"
#include "field_engine.h"
#include "pca9685.h"
#include "platform_lock.h"
#include "config.h"

#include "driver/gpio.h"

const CoilHw COIL_HW[4] = {
  { 0, 1, PIN_EN_M1 },
  { 2, 3, PIN_EN_M2 },
  { 4, 5, PIN_EN_M3 },
  { 6, 7, PIN_EN_M4 },
};

CoilState coils[NUM_COILS];
uint8_t g_pwm_r[NUM_COILS];
uint8_t g_pwm_l[NUM_COILS];
volatile int16_t g_signed_d[NUM_COILS];
uint8_t g_contour_active;

static void en_write(uint8_t pin, uint8_t on)
{
  gpio_set_level((gpio_num_t)pin, on ? 1 : 0);
}

void Coil_GpioInit(void)
{
  gpio_config_t io = {0};
  io.pin_bit_mask = (1ULL << PIN_EN_M1) | (1ULL << PIN_EN_M2) |
                    (1ULL << PIN_EN_M3) | (1ULL << PIN_EN_M4);
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);
  en_write(PIN_EN_M1, 0);
  en_write(PIN_EN_M2, 0);
  en_write(PIN_EN_M3, 0);
  en_write(PIN_EN_M4, 0);
}

void Coil_SetPwm(uint8_t idx, uint8_t rpwm, uint8_t lpwm)
{
  if (idx >= NUM_COILS) return;
  field_irq_lock();
  if (g_pwm_r[idx] != rpwm || g_pwm_l[idx] != lpwm) {
    g_pwm_r[idx] = rpwm;
    g_pwm_l[idx] = lpwm;
    g_pca_dirty = 1;
  }
  field_irq_unlock();
}

static uint8_t field_coil_driven(uint8_t idx)
{
  if (!g_rotate_active || idx >= NUM_COILS) return 0;
  return 1;
}

void Coil_ApplyOutputs(uint8_t idx)
{
  const CoilHw *hw;
  CoilState *st;
  uint16_t duty = 0;

  if (idx >= NUM_COILS) return;
  if (g_rotate_active && idx < NUM_ROTATE_COILS) return;

  hw = &COIL_HW[idx];
  st = &coils[idx];
  if (st->enabled && st->magnitude > 0) {
    duty = st->duty_applied;
  }

  if (!st->enabled || duty == 0) {
    Coil_SetPwm(idx, 0, 0);
    en_write(hw->en_pin, 0);
    PCA9685_Flush();
    return;
  }

  en_write(hw->en_pin, 1);
  if (st->dir == DIR_FWD) {
    Coil_SetPwm(idx, (uint8_t)duty, 0);
  } else {
    Coil_SetPwm(idx, 0, (uint8_t)duty);
  }
  PCA9685_Flush();
}

void Coil_ApplySignedDuty(uint8_t idx, int16_t signed_duty)
{
  const CoilHw *hw;
  uint16_t mag;

  if (idx >= NUM_COILS) return;
  hw = &COIL_HW[idx];

  if (signed_duty > (int16_t)PWM_ARR) signed_duty = (int16_t)PWM_ARR;
  if (signed_duty < -(int16_t)PWM_ARR) signed_duty = -(int16_t)PWM_ARR;

  if (signed_duty >= 0) {
    mag = (uint16_t)signed_duty;
    if (mag == 0) {
      Coil_SetPwm(idx, 0, 0);
      if (field_coil_driven(idx)) {
        en_write(hw->en_pin, 1);
      } else {
        en_write(hw->en_pin, 0);
      }
    } else {
      en_write(hw->en_pin, 1);
      Coil_SetPwm(idx, (uint8_t)mag, 0);
    }
  } else {
    mag = (uint16_t)(-signed_duty);
    en_write(hw->en_pin, 1);
    Coil_SetPwm(idx, 0, (uint8_t)mag);
  }

  coils[idx].duty_applied = mag;
  coils[idx].dir = (signed_duty >= 0) ? DIR_FWD : DIR_REV;
  g_signed_d[idx] = signed_duty;
}

void Coil_ApplyPairedSignedDuty(uint8_t a, uint8_t b, int16_t signed_duty)
{
  Coil_ApplySignedDuty(a, signed_duty);
  Coil_ApplySignedDuty(b, signed_duty);
}

void Coil_AllOff(void)
{
  int i;
  Rotate_Stop();
  g_contour_active = 0;
  for (i = 0; i < NUM_COILS; i++) {
    coils[i].enabled = 0;
    coils[i].duty_applied = 0;
    Coil_ApplyOutputs((uint8_t)i);
  }
}

void Coil_SetEnabled(uint8_t idx, uint8_t on)
{
  if (idx >= NUM_COILS) return;
  Rotate_StopIfDcConflict(idx);
  coils[idx].enabled = on ? 1 : 0;
  if (!on) coils[idx].duty_applied = 0;
  else coils[idx].duty_applied = coils[idx].magnitude;
  Coil_ApplyOutputs(idx);
}

void Coil_SetMagnitude(uint8_t idx, uint8_t mag)
{
  if (idx >= NUM_COILS) return;
  Rotate_StopIfDcConflict(idx);
  coils[idx].magnitude = mag;
  coils[idx].duty_applied = mag;
  Coil_ApplyOutputs(idx);
}

void Coil_SetDir(uint8_t idx, CoilDir d)
{
  if (idx >= NUM_COILS) return;
  Rotate_StopIfDcConflict(idx);
  coils[idx].dir = d;
  Coil_ApplyOutputs(idx);
}
