/**
 * @file main.c
 * @brief STM32H743VIT6 (WeAct Mini) coil driver for 4× BTS7960 / IBT-2.
 *
 * Four coils sit 90° apart around the sample, each tilted ~45° so a coil
 * contributes both horizontal and vertical field at the centre.
 *
 * Lab frame: Y is vertical (out of plane). X and Z are horizontal.
 *   M1/M3 — YZ pair (revolve x; rotate-Y cosine)
 *   M2/M4 — XY pair (revolve z; rotate-Y sine)
 *
 * Dual modes:
 *   DC        — constant magnitude/dir per coil (open- or closed-loop IS)
 *   Rotate Y  — field in the XZ plane, all four coils. M1=M3 ∝ cos ωt,
 *               M2=M4 ∝ sin ωt (pairs in the same current direction, 90° apart).
 *   Revolve Z — field in the XY plane. Only M2=I1, M4=I2
 *               (Ah=amp*h/100, Av=amp*v/100; 100/100 → constant |B|=amp).
 *   Revolve X — field in the YZ plane. Only M1=I1, M3=I2 (same Ah/Av ellipse).
 *   Revolve tilt — field about a fixed XZ-plane axis n=(cos γ, 0, sin γ).
 *               All four coils. Equal vertical split; see field_tilt.c.
 *
 * Shared freq (0.01–50 Hz) + amp; TIM6 sine ISR; amp ramps ~1.3 s.
 * PWM carrier is a PCA9685 (~1 kHz, 12-bit) over I2C — not STM32 timers.
 *
 * Serial CLI @ 115200 8N1:
 *   USART1 PA9 TX / PA10 RX  — USB-UART Serial Monitor (human, with echo)
 *   USART3 PD8 TX / PD9 RX  — ESP32-S3 WiFi bridge (machine, no echo)
 *
 * ESP32 wiring (3.3 V logic, common GND):
 *   ESP32 TX (GPIO17) → STM32 PD9 (USART3_RX)
 *   ESP32 RX (GPIO18) ← STM32 PD8 (USART3_TX)
 *
 * Open PlatformIO Serial Monitor and type "help", or join the ESP32 SoftAP
 * and open http://192.168.4.1/
 *
 * Wiring:
 *   I2C1  PB8 SCL / PB9 SDA  → PCA9685 (addr 0x40). Tie PCA OE to GND.
 *   I2C2  PB10 SCL / PB11 SDA → MLX90393 (3.3 V, CS high). Do not share I2C1.
 *   PCA ch0/1 = M1 RPWM/LPWM, ch2/3 = M2, ch4/5 = M3, ch6/7 = M4.
 *   EN stays on the STM32 (same header clusters as before). IS on PA0–PA3.
 *
 *   Coil  PCA PWM          EN      IS(ADC)
 *   M1    ch0 RPWM / ch1 LPWM   PD10    PA0
 *   M2    ch2 RPWM / ch3 LPWM   PE4     PA1
 *   M3    ch4 RPWM / ch5 LPWM   PC4     PA2
 *   M4    ch6 RPWM / ch7 LPWM   PC5     PA3
 *
 * Onboard ST7735 (SPI4): PE12 SCK, PE14 MOSI, PE11 CS, PE13 DC, PE10 BL.
 *
 * BTS7960 / IBT-2: tie R_EN+L_EN to EN, VCC=5 V logic, VS = coil supply.
 * IS → MCU ADC through the module sense resistor (see IS_RIS_OHM).
 */

#include "stm32h7xx_hal.h"
#include "st7735.h"
#include "display_ui.h"
#include "mlx90393.h"
#include "field_tilt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>

static int str_ieq(const char *a, const char *b)
{
  if (!a || !b) return 0;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    a++;
    b++;
  }
  return *a == *b;
}

#ifndef NUM_COILS
#define NUM_COILS 4
#endif
#if NUM_COILS != 4
#error "This firmware drives four coils (M1–M4)"
#endif

#ifndef PWM_TARGET_HZ
#define PWM_TARGET_HZ 1000U            /* PCA9685 carrier; chip max ~1.5 kHz */
#endif

#define PWM_ARR                 255U   /* duty command 0..255 (mapped to 12-bit) */
#define UART_BAUD               115200U
#define CLI_LINE_MAX            128
#define CURRENT_LOOP_HZ         200U   /* sense / closed-loop update rate */

#ifndef PCA9685_I2C_ADDR
#define PCA9685_I2C_ADDR        0x40u  /* 7-bit; A0–A5 = 0 on typical boards */
#endif
#define PCA9685_OSC_HZ          25000000u
#define PCA_NCHAN               8      /* 4 coils × RPWM+LPWM */

/* AC field programs (rotate Y / revolve X|Z) share the sine ISR */
#define NUM_ROTATE_COILS        4
#define SINE_UPDATE_HZ          1000U  /* ISR samples; I2C flush is slower */
#define SINE_LUT_SIZE           360
#define SINE_LUT_45             45u
#define SINE_LUT_90             90u
#define ROTATE_FREQ_MIN_HZ      0.01f
#define ROTATE_FREQ_MAX_HZ      50.0f
#ifndef ROTATE_RAMP_STEP
#define ROTATE_RAMP_STEP        1u   /* live-amp LSB per 200 Hz tick; 0↔255 ≈ 1.3 s */
#endif
/* Revolve ellipse: Ah = amp*h/100, Av = amp*v/100 (100/100 = circle in PWM-proxy B). */
#ifndef REVOLVE_H_PCT_DEFAULT
#define REVOLVE_H_PCT_DEFAULT   100u
#endif
#ifndef REVOLVE_V_PCT_DEFAULT
#define REVOLVE_V_PCT_DEFAULT   100u
#endif
#define REVOLVE_HV_PCT_MIN      1u
#define REVOLVE_HV_PCT_MAX      200u
#ifndef TILT_GAMMA_DEFAULT_DEG
#define TILT_GAMMA_DEFAULT_DEG  45.0f
#endif
/* Soft OC must persist this many 200 Hz ticks before amp derate. */
#ifndef ROTATE_OC_HYST_TICKS
#define ROTATE_OC_HYST_TICKS    3u
#endif
/* While AC field runs, flush PCA every N UART TX bytes so json/status cannot starve PWM. */
#ifndef UART_FLUSH_PCA_EVERY
#define UART_FLUSH_PCA_EVERY    32u
#endif
#ifndef TELEPLOT_HZ
#define TELEPLOT_HZ             50u  /* USART1 Teleplot sample rate when enabled */
#endif
#ifndef MLX_POLL_HZ
#define MLX_POLL_HZ             50u
#endif
#ifndef MLX_CAL_SETTLE_MS
#define MLX_CAL_SETTLE_MS       80u
#endif
#ifndef MLX_CAL_AVG
#define MLX_CAL_AVG             8u
#endif

/* BTS7960 sense: Iload ≈ Vis * k_ris / Ris. Datasheet k ≈ 8500. */
#ifndef IS_RIS_OHM
#define IS_RIS_OHM              1000.0f
#endif
#ifndef IS_K_RATIO
#define IS_K_RATIO              8500.0f
#endif
#define I_FULLSCALE_A           2.0f   /* magnitude 255 maps to this target */
#define ADC_VREF                3.3f
#define ADC_MAX_COUNTS          65535.0f /* 16-bit oversampling result space */

typedef enum {
  DIR_FWD = 0, /* current / B out of RPWM path */
  DIR_REV = 1
} CoilDir;

typedef enum {
  CTRL_OPEN = 0,   /* duty = magnitude */
  CTRL_CLOSED = 1  /* PI on IS toward I_ref(magnitude) */
} CtrlMode;

typedef enum {
  ROT_CW = 0,  /* rotate Y: M2∥M4 = +sin. revolve: +ω */
  ROT_CCW = 1  /* rotate Y: M2∥M4 = −sin. revolve: ω → −ω */
} RotateDir;

typedef enum {
  FIELD_ROTATE_Y = 0,     /* XZ plane; M1=M3 cos, M2=M4 sin */
  FIELD_REVOLVE_X = 1,    /* YZ plane; M1=I1, M3=I2 */
  FIELD_REVOLVE_Z = 2,    /* XY plane; M2=I1, M4=I2 */
  FIELD_REVOLVE_TILT = 3  /* fixed XZ-tilt axis; all four coils */
} FieldProgram;

typedef struct {
  uint8_t       pca_rpwm;  /* PCA9685 channel 0..15 */
  uint8_t       pca_lpwm;
  GPIO_TypeDef *en_port;
  uint16_t      en_pin;
  uint32_t      adc_channel;
} CoilHw;

typedef struct {
  uint8_t   enabled;
  uint8_t   magnitude;     /* 0..255 */
  CoilDir   dir;
  float     i_meas_a;
  float     i_limit_a;     /* soft limit; 0 disables */
  float     pi_integral;
  uint16_t  duty_applied;  /* actual CCR after limit/loop */
} CoilState;

/* -------------------- pin / PCA map -------------------- */
static const CoilHw COIL_HW[4] = {
  /* M1 YZ B1 / rotate-Y cos */ { 0, 1, GPIOD, GPIO_PIN_10, ADC_CHANNEL_16 }, /* IS PA0 */
  /* M2 XY B1 / rotate-Y sin */ { 2, 3, GPIOE, GPIO_PIN_4,  ADC_CHANNEL_17 }, /* IS PA1 */
  /* M3 YZ B2 / rotate-Y cos */ { 4, 5, GPIOC, GPIO_PIN_4,  ADC_CHANNEL_14 }, /* IS PA2 */
  /* M4 XY B2 / rotate-Y sin */ { 6, 7, GPIOC, GPIO_PIN_5,  ADC_CHANNEL_15 }, /* IS PA3 */
};

/* PA0..PA3 ADC1 channels on H743:
 *   PA0 ADC1_INP16, PA1 ADC1_INP17, PA2 ADC1_INP14, PA3 ADC1_INP15
 */

static UART_HandleTypeDef huart1;
static UART_HandleTypeDef huart3;
static ADC_HandleTypeDef  hadc1;
static I2C_HandleTypeDef  hi2c1;
static I2C_HandleTypeDef  hi2c2;
static TIM_HandleTypeDef  htim6;
static Mlx90393           g_mlx;
static uint8_t    g_mlx_src[3] = {0, 1, 2}; /* lab X,Y,Z from chip axis */
static int8_t     g_mlx_sgn[3] = {1, 1, 1};
static float      g_mlx_b0_mt[3]; /* Earth + bias, lab mT */
static uint8_t    g_mlx_b0_ok;
static float      g_mlx_lab_mt[3];
static float      g_mlx_babs_mt;
static uint8_t    g_mlx_sat;
static uint8_t    g_mlx_en = 1; /* poll when the chip is present */

static uint8_t           g_pwm_r[NUM_COILS]; /* 0..255 RPWM shadow */
static uint8_t           g_pwm_l[NUM_COILS];
static volatile uint8_t  g_pca_dirty;
static uint8_t           g_pca_ok;

static CoilState coils[NUM_COILS];
static float     g_i_offset_a[NUM_COILS]; /* IS idle offset, subtracted in Sense_ReadAmps */
static CtrlMode  g_ctrl_mode = CTRL_OPEN;
static uint8_t   g_contour_active;

/* AC field state (open-loop sine on M1–M4) */
static volatile uint8_t  g_rotate_active; /* 1 = sine ISR running (rotate or revolve) */
static float             g_rotate_freq_hz = 1.0f;
static uint8_t           g_rotate_amp = 0;
static volatile uint8_t  g_rotate_amp_live = 0; /* ramp + soft-clamp */
static RotateDir         g_rotate_dir = ROT_CW;      /* applied in sine ISR */
static RotateDir         g_rotate_dir_want = ROT_CW; /* operator setpoint */
static FieldProgram      g_field_prog = FIELD_ROTATE_Y;
static FieldProgram      g_field_prog_want = FIELD_ROTATE_Y;
static uint8_t           g_revolve_h_pct = (uint8_t)REVOLVE_H_PCT_DEFAULT;
static uint8_t           g_revolve_v_pct = (uint8_t)REVOLVE_V_PCT_DEFAULT;
static float             g_tilt_gamma_want = TILT_GAMMA_DEFAULT_DEG;
static volatile float    g_tilt_gamma_live = TILT_GAMMA_DEFAULT_DEG;
static volatile float    g_tilt_sin_g = FIELD_TILT_C; /* sin/cos 45° */
static volatile float    g_tilt_cos_g = FIELD_TILT_C;
static volatile float    g_tilt_scale = 1.0f;
static volatile uint8_t  g_rotate_stop_pending;
static volatile uint32_t g_phase;
static volatile uint32_t g_phase_inc;
static volatile uint8_t  g_revolve_phase_hold; /* 1 = freeze phase (start/stop sine swell) */
static volatile uint8_t  g_revolve_env_peak;   /* quarter-sine peak while hold is set */
static volatile uint8_t  g_revolve_park;       /* 1 = park at g_revolve_corner */
static volatile uint8_t  g_revolve_corner;     /* lab Q0..Q3 when parked */
static volatile uint8_t  g_revolve_travel;     /* 1 = stepping toward park corner */
static uint8_t           g_field_sense_rr;      /* round-robin coil index while AC field on */
static uint8_t           g_rotate_oc_streak;    /* consecutive over-limit ticks */
static uint8_t           g_teleplot_en;         /* USART1 Teleplot stream (off by default) */

/* #region agent log */
static volatile uint32_t g_dbg_ore1, g_dbg_ore3;
static volatile uint32_t g_dbg_empty_cr, g_dbg_cli_lines;
static volatile uint32_t g_dbg_isr;
static volatile uint8_t  g_dbg_over;
static volatile uint32_t g_dbg_pair_mis;
static volatile int16_t  g_signed_d[NUM_COILS];
/* #endregion */

/* Q15 sine table: sin(2π i / 360) * 32767 */
static const int16_t SINE_LUT[SINE_LUT_SIZE] = {
        0,     572,    1144,    1715,    2286,    2856,    3425,    3993,
     4560,    5126,    5690,    6252,    6813,    7371,    7927,    8481,
     9032,    9580,   10126,   10668,   11207,   11743,   12275,   12803,
    13328,   13848,   14364,   14876,   15383,   15886,   16383,   16876,
    17364,   17846,   18323,   18794,   19260,   19720,   20173,   20621,
    21062,   21497,   21925,   22347,   22762,   23170,   23571,   23964,
    24351,   24730,   25101,   25465,   25821,   26169,   26509,   26841,
    27165,   27481,   27788,   28087,   28377,   28659,   28932,   29196,
    29451,   29697,   29934,   30162,   30381,   30591,   30791,   30982,
    31163,   31335,   31498,   31650,   31794,   31927,   32051,   32165,
    32269,   32364,   32448,   32523,   32587,   32642,   32687,   32722,
    32747,   32762,   32767,   32762,   32747,   32722,   32687,   32642,
    32587,   32523,   32448,   32364,   32269,   32165,   32051,   31927,
    31794,   31650,   31498,   31335,   31163,   30982,   30791,   30591,
    30381,   30162,   29934,   29697,   29451,   29196,   28932,   28659,
    28377,   28087,   27788,   27481,   27165,   26841,   26509,   26169,
    25821,   25465,   25101,   24730,   24351,   23964,   23571,   23170,
    22762,   22347,   21925,   21497,   21062,   20621,   20173,   19720,
    19260,   18794,   18323,   17846,   17364,   16876,   16383,   15886,
    15383,   14876,   14364,   13848,   13328,   12803,   12275,   11743,
    11207,   10668,   10126,    9580,    9032,    8481,    7927,    7371,
     6813,    6252,    5690,    5126,    4560,    3993,    3425,    2856,
     2286,    1715,    1144,     572,       0,    -572,   -1144,   -1715,
    -2286,   -2856,   -3425,   -3993,   -4560,   -5126,   -5690,   -6252,
    -6813,   -7371,   -7927,   -8481,   -9032,   -9580,  -10126,  -10668,
   -11207,  -11743,  -12275,  -12803,  -13328,  -13848,  -14364,  -14876,
   -15383,  -15886,  -16384,  -16876,  -17364,  -17846,  -18323,  -18794,
   -19260,  -19720,  -20173,  -20621,  -21062,  -21497,  -21925,  -22347,
   -22762,  -23170,  -23571,  -23964,  -24351,  -24730,  -25101,  -25465,
   -25821,  -26169,  -26509,  -26841,  -27165,  -27481,  -27788,  -28087,
   -28377,  -28659,  -28932,  -29196,  -29451,  -29697,  -29934,  -30162,
   -30381,  -30591,  -30791,  -30982,  -31163,  -31335,  -31498,  -31650,
   -31794,  -31927,  -32051,  -32165,  -32269,  -32364,  -32448,  -32523,
   -32587,  -32642,  -32687,  -32722,  -32747,  -32762,  -32767,  -32762,
   -32747,  -32722,  -32687,  -32642,  -32587,  -32523,  -32448,  -32364,
   -32269,  -32165,  -32051,  -31927,  -31794,  -31650,  -31498,  -31335,
   -31163,  -30982,  -30791,  -30591,  -30381,  -30162,  -29934,  -29697,
   -29451,  -29196,  -28932,  -28659,  -28377,  -28087,  -27788,  -27481,
   -27165,  -26841,  -26509,  -26169,  -25821,  -25465,  -25101,  -24730,
   -24351,  -23964,  -23571,  -23170,  -22762,  -22347,  -21925,  -21497,
   -21062,  -20621,  -20173,  -19720,  -19260,  -18794,  -18323,  -17846,
   -17364,  -16876,  -16384,  -15886,  -15383,  -14876,  -14364,  -13848,
   -13328,  -12803,  -12275,  -11743,  -11207,  -10668,  -10126,   -9580,
    -9032,   -8481,   -7927,   -7371,   -6813,   -6252,   -5690,   -5126,
    -4560,   -3993,   -3425,   -2856,   -2286,   -1715,   -1144,    -572
};

typedef struct {
  USART_TypeDef      *usart;
  UART_HandleTypeDef *hal;
  char                buf[CLI_LINE_MAX];
  uint16_t            len;
  uint8_t             echo; /* 1 = human serial monitor */
} CliPort;

static CliPort cli_ports[2];
static CliPort *cli_reply; /* UART that receives command replies */

#define UART_RXQ_SIZE 256U
typedef struct {
  volatile uint8_t  buf[UART_RXQ_SIZE];
  volatile uint16_t head;
  volatile uint16_t tail;
  volatile uint32_t *ore_ctr;
  USART_TypeDef    *usart;
} UartRxQ;
static UartRxQ rxq1, rxq3;

static uint32_t last_loop_ms;

/* -------------------- prototypes -------------------- */
void Error_Handler(void);
static void SystemClock_Config(void);
static void CPU_CACHE_Enable(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C2_Init(void);
static void MX_TIM6_Init(void);
static void Mlx_ApplySample(const MlxSample *s);
static void Mlx_PrintStatus(void);
static void PCA9685_Init(void);
static void PCA9685_Flush(void);
static void Display_PushStatus(void);
static void Coil_ApplyOutputs(uint8_t idx);
static void Coil_ApplySignedDuty(uint8_t idx, int16_t signed_duty);
static void Coil_AllOff(void);
static void Rotate_RecalcPhaseInc(void);
static void Rotate_Stop(void);
static void Rotate_RequestStop(void);
static void Field_Start(FieldProgram p);
static void Rotate_SetDir(RotateDir d);
static void Field_SetProgram(FieldProgram p);
static void Field_ApplyOptionalFreqAmp(int argc, char **argv, int start);
static void Tilt_CommitGamma(float deg);
static void Tilt_SetGamma(float deg);
static void Revolve_ClearPark(void);
static void Revolve_GotoCorner(uint8_t q);
static void Rotate_StopIfDcConflict(uint8_t idx);
static void Rotate_RampUpdate(void);
static float Sense_ReadAmps(uint8_t idx);
static void Sense_CalibrateOffsets(void);
static void CurrentLoop_Update(void);
static void CLI_PollPort(CliPort *port);
static void CLI_HandleLine(char *line);
static void uart_write(const char *s);
static void uart_printf(const char *fmt, ...);

/* -------------------- UART helpers (register-level; HAL timeout-safe) ---- */
static void uart_clear_errors_usart(USART_TypeDef *u, UART_HandleTypeDef *h)
{
  if (u->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
    /* #region agent log */
    if (u->ISR & USART_ISR_ORE) {
      if (u == USART1) g_dbg_ore1++;
      else if (u == USART3) g_dbg_ore3++;
    }
    /* #endregion */
    u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
  }
  if (h) {
    if (h->gState != HAL_UART_STATE_READY) h->gState = HAL_UART_STATE_READY;
    if (h->RxState != HAL_UART_STATE_READY) h->RxState = HAL_UART_STATE_READY;
  }
}

static void uart_rxq_push_isr(UartRxQ *q)
{
  USART_TypeDef *u = q->usart;
  uint32_t isr = u->ISR;
  if (isr & USART_ISR_ORE) {
    /* #region agent log */
    if (q->ore_ctr) (*q->ore_ctr)++;
    /* #endregion */
    u->ICR = USART_ICR_ORECF;
  }
  if (isr & (USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
    u->ICR = USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
  }
  /* Keep RDR bytes (ORE used to discard the pending char). */
  while (u->ISR & USART_ISR_RXNE_RXFNE) {
    uint8_t c = (uint8_t)(u->RDR & 0xFFU);
    uint16_t next = (uint16_t)((q->head + 1U) & (UART_RXQ_SIZE - 1U));
    if (next != q->tail) {
      q->buf[q->head] = c;
      q->head = next;
    }
  }
}

static int uart_rxq_pop(UartRxQ *q, uint8_t *ch)
{
  uint16_t tail = q->tail;
  if (tail == q->head) return 0;
  *ch = q->buf[tail];
  q->tail = (uint16_t)((tail + 1U) & (UART_RXQ_SIZE - 1U));
  return 1;
}

static int uart_getc_usart(USART_TypeDef *u, UART_HandleTypeDef *h, uint8_t *ch)
{
  (void)h;
  if (u == USART1) return uart_rxq_pop(&rxq1, ch);
  if (u == USART3) return uart_rxq_pop(&rxq3, ch);
  return 0;
}

static void uart_rxq_init(UartRxQ *q, USART_TypeDef *u, volatile uint32_t *ore_ctr,
                          IRQn_Type irqn)
{
  q->head = 0;
  q->tail = 0;
  q->ore_ctr = ore_ctr;
  q->usart = u;
  u->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
  HAL_NVIC_SetPriority(irqn, 1, 0); /* higher than TIM6 (prio 2) */
  HAL_NVIC_EnableIRQ(irqn);
}

static void uart_putc(uint8_t ch)
{
  USART_TypeDef *u = cli_reply ? cli_reply->usart : USART1;
  uint32_t start = HAL_GetTick();
  while ((u->ISR & USART_ISR_TXE_TXFNF) == 0U) {
    if ((HAL_GetTick() - start) > 50U) {
      return;
    }
  }
  u->TDR = ch;
}

static void uart_write(const char *s)
{
  if (!s) return;
  uint16_t n = 0;
  while (*s) {
    uart_putc((uint8_t)*s++);
    n++;
    /* Keep PCA updating during long json/status dumps so the field never freezes. */
    if (g_rotate_active && (n % (uint16_t)UART_FLUSH_PCA_EVERY) == 0U) {
      PCA9685_Flush();
    }
  }
}

static void uart_printf(const char *fmt, ...)
{
  char tmp[384];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  uart_write(tmp);
}

static void uart_write_to(CliPort *port, const char *s)
{
  CliPort *prev = cli_reply;
  cli_reply = port;
  uart_write(s);
  cli_reply = prev;
}

/* 45° coil tilt: cos45 = sin45 = √2/2. Signed PWM is the B/current proxy. */
#define LAB_COS45  0.7071067811865476f

static void lab_from_pair(int16_t b1, int16_t b2, float *horiz, float *vert)
{
  float s1 = (float)b1;
  float s2 = (float)b2;
  *horiz = -LAB_COS45 * (s1 + s2);
  *vert  =  LAB_COS45 * (s1 - s2);
}

/* Teleplot (https://github.com/nesnes/teleplot) on USART1 only — opt-in. */
static void Teleplot_Emit(void)
{
  int16_t sd[NUM_COILS];
  uint8_t pr[NUM_COILS], pl[NUM_COILS];
  uint8_t amp_live;
  float bx, by_xy, bz, by_yz, by, babs;
  char line[48];
  CliPort *prev;

  if (!g_teleplot_en) return;

  __disable_irq();
  memcpy(sd, (const void *)g_signed_d, sizeof(sd));
  memcpy(pr, g_pwm_r, sizeof(pr));
  memcpy(pl, g_pwm_l, sizeof(pl));
  amp_live = g_rotate_amp_live;
  __enable_irq();

  /* DC path may not refresh g_signed_d — reconstruct from RPWM/LPWM shadows. */
  if (!g_rotate_active) {
    for (int i = 0; i < NUM_COILS; i++) {
      if (pr[i]) sd[i] = (int16_t)pr[i];
      else if (pl[i]) sd[i] = (int16_t)(-(int16_t)pl[i]);
      else sd[i] = 0;
    }
  }

  /* M2/M4 = XY pair (Bx, By); M1/M3 = YZ pair (Bz, By). */
  lab_from_pair(sd[1], sd[3], &bx, &by_xy);
  lab_from_pair(sd[0], sd[2], &bz, &by_yz);
  by = by_xy + by_yz;
  babs = sqrtf(bx * bx + by * by + bz * bz);

  prev = cli_reply;
  cli_reply = &cli_ports[0]; /* force USART1 */
  for (int i = 0; i < NUM_COILS; i++) {
    snprintf(line, sizeof(line), ">M%d_signed:%d\n", i + 1, (int)sd[i]);
    uart_write(line);
    snprintf(line, sizeof(line), ">M%d_rpwm:%u\n", i + 1, (unsigned)pr[i]);
    uart_write(line);
    snprintf(line, sizeof(line), ">M%d_lpwm:%u\n", i + 1, (unsigned)pl[i]);
    uart_write(line);
  }
  snprintf(line, sizeof(line), ">amp_live:%u\n", (unsigned)amp_live);
  uart_write(line);
  snprintf(line, sizeof(line), ">Bx:%.2f\n", (double)bx);
  uart_write(line);
  snprintf(line, sizeof(line), ">By:%.2f\n", (double)by);
  uart_write(line);
  snprintf(line, sizeof(line), ">Bz:%.2f\n", (double)bz);
  uart_write(line);
  snprintf(line, sizeof(line), ">Babs:%.2f\n", (double)babs);
  uart_write(line);
  {
    float sg = g_tilt_sin_g;
    float cg = g_tilt_cos_g;
    float gdeg = g_tilt_gamma_live;
    float scale = g_tilt_scale;
    float bh = -bx * sg + bz * cg;
    float bv = by;
    float bn = bx * cg + bz * sg;
    snprintf(line, sizeof(line), ">gamma:%.2f\n", (double)gdeg);
    uart_write(line);
    snprintf(line, sizeof(line), ">Bh:%.2f\n", (double)bh);
    uart_write(line);
    snprintf(line, sizeof(line), ">Bv:%.2f\n", (double)bv);
    uart_write(line);
    snprintf(line, sizeof(line), ">Bn:%.2f\n", (double)bn);
    uart_write(line);
    snprintf(line, sizeof(line), ">tilt_scale:%.4f\n", (double)scale);
    uart_write(line);
  }
  snprintf(line, sizeof(line), ">Bxy:%.2f:%.2f|xy\n", (double)bx, (double)by);
  uart_write(line);
  snprintf(line, sizeof(line), ">Bxz:%.2f:%.2f|xy\n", (double)bz, (double)bx);
  uart_write(line);
  snprintf(line, sizeof(line), ">Byz:%.2f:%.2f|xy\n", (double)bz, (double)by);
  uart_write(line);
  if (g_mlx.present) {
    snprintf(line, sizeof(line), ">Bx_m:%.3f\n", (double)g_mlx_lab_mt[0]);
    uart_write(line);
    snprintf(line, sizeof(line), ">By_m:%.3f\n", (double)g_mlx_lab_mt[1]);
    uart_write(line);
    snprintf(line, sizeof(line), ">Bz_m:%.3f\n", (double)g_mlx_lab_mt[2]);
    uart_write(line);
    snprintf(line, sizeof(line), ">Babs_m:%.3f\n", (double)g_mlx_babs_mt);
    uart_write(line);
    snprintf(line, sizeof(line), ">Bxy_m:%.3f:%.3f|xy\n",
             (double)g_mlx_lab_mt[0], (double)g_mlx_lab_mt[1]);
    uart_write(line);
    snprintf(line, sizeof(line), ">Byz_m:%.3f:%.3f|xy\n",
             (double)g_mlx_lab_mt[2], (double)g_mlx_lab_mt[1]);
    uart_write(line);
    snprintf(line, sizeof(line), ">Bxz_m:%.3f:%.3f|xy\n",
             (double)g_mlx_lab_mt[2], (double)g_mlx_lab_mt[0]);
    uart_write(line);
  }
  cli_reply = prev;
}

/* -------------------- clock / cache -------------------- */
static void CPU_CACHE_Enable(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();
}

static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
  }

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;    /* 25 MHz / 5 = 5 MHz */
  RCC_OscInitStruct.PLL.PLLN = 192;  /* 5 * 192 = 960 MHz VCO */
  RCC_OscInitStruct.PLL.PLLP = 2;    /* SYSCLK = 480 MHz */
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2; /* 240 MHz HCLK */
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
    Error_Handler();
  }
}

/* -------------------- GPIO / UART / TIM / ADC -------------------- */
static void gpio_clk_enable(GPIO_TypeDef *port)
{
  if (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
  else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
  else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
  else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
  else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  /* Enable pins: push-pull, start LOW (drivers disabled) */
  for (int i = 0; i < NUM_COILS; i++) {
    gpio_clk_enable(COIL_HW[i].en_port);
    g.Pin = COIL_HW[i].en_pin;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(COIL_HW[i].en_port, &g);
    HAL_GPIO_WritePin(COIL_HW[i].en_port, COIL_HW[i].en_pin, GPIO_PIN_RESET);
  }

  /* Status LED on WeAct Mini: PE3 */
  g.Pin = GPIO_PIN_3;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &g);
}

static void MX_USART1_UART_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  g.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOA, &g);

  huart1.Instance = USART1;
  huart1.Init.BaudRate = UART_BAUD;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK) {
    Error_Handler();
  }
#if defined(USART_CR1_FIFOEN)
  HAL_UARTEx_DisableFifoMode(&huart1);
#endif
  uart_clear_errors_usart(USART1, &huart1);
  uart_rxq_init(&rxq1, USART1, &g_dbg_ore1, USART1_IRQn);
}

/* ESP32 WiFi bridge link — PD8 TX / PD9 RX @ 115200 */
static void MX_USART3_UART_Init(void)
{
  GPIO_InitTypeDef g = {0};
  RCC_PeriphCLKInitTypeDef per = {0};

  /* H7: force USART3 kernel clock = APB1 (D2PCLK1) so baud matches USART1. */
  per.PeriphClockSelection = RCC_PERIPHCLK_USART3;
  per.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&per) != HAL_OK) {
    Error_Handler();
  }

  __HAL_RCC_USART3_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  g.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF7_USART3;
  HAL_GPIO_Init(GPIOD, &g);

  huart3.Instance = USART3;
  huart3.Init.BaudRate = UART_BAUD;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK) {
    Error_Handler();
  }
#if defined(USART_CR1_FIFOEN)
  HAL_UARTEx_DisableFifoMode(&huart3);
#endif
  uart_clear_errors_usart(USART3, &huart3);
  uart_rxq_init(&rxq3, USART3, &g_dbg_ore3, USART3_IRQn);
}

static uint32_t timer_clock_hz(TIM_TypeDef *tim)
{
  /* With APB1/APB2 divider = 2, timer clocks are 2× PCLK (= HCLK = 240 MHz). */
  (void)tim;
  return HAL_RCC_GetHCLKFreq();
}

/* I2C1 PB8 SCL / PB9 SDA @ 400 kHz → PCA9685. TIMINGR for I2CCLK=120 MHz. */
static void MX_I2C1_Init(void)
{
  GPIO_InitTypeDef g = {0};
  RCC_PeriphCLKInitTypeDef per = {0};

  per.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
  per.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&per) != HAL_OK) {
    Error_Handler();
  }

  __HAL_RCC_I2C1_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  g.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  g.Mode = GPIO_MODE_AF_OD;
  g.Pull = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &g);

  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x3080192E; /* ~400 kHz @ 120 MHz I2CCLK */
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
    Error_Handler();
  }
  HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);
  HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0);
}

/* I2C2 PB10 SCL / PB11 SDA @ 400 kHz → MLX90393. Same I2CCLK as I2C1. */
static void MX_I2C2_Init(void)
{
  GPIO_InitTypeDef g = {0};
  RCC_PeriphCLKInitTypeDef per = {0};

  per.PeriphClockSelection = RCC_PERIPHCLK_I2C123;
  per.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&per) != HAL_OK) {
    Error_Handler();
  }
  __HAL_RCC_I2C2_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  g.Pin = GPIO_PIN_10 | GPIO_PIN_11;
  g.Mode = GPIO_MODE_AF_OD;
  g.Pull = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF4_I2C2;
  HAL_GPIO_Init(GPIOB, &g);

  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x3080192E; /* ~400 kHz @ 120 MHz I2CCLK */
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK) {
    Error_Handler();
  }
  HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE);
  HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0);
}

static HAL_StatusTypeDef pca_write(uint8_t reg, const uint8_t *data, uint16_t n)
{
  uint8_t buf[64];
  if (n + 1U > sizeof(buf)) return HAL_ERROR;
  buf[0] = reg;
  if (n && data) memcpy(buf + 1, data, n);
  return HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(PCA9685_I2C_ADDR << 1),
                                 buf, (uint16_t)(n + 1U), 20);
}

static HAL_StatusTypeDef pca_write_u8(uint8_t reg, uint8_t val)
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
  out[0] = 0; /* ON_L */
  out[1] = 0; /* ON_H */
  if (off == 0) {
    out[2] = 0;
    out[3] = 0x10; /* FULL_OFF */
  } else {
    out[2] = (uint8_t)(off & 0xFFU);
    out[3] = (uint8_t)((off >> 8) & 0x0FU);
  }
}

static void PCA9685_Flush(void)
{
  uint8_t r[NUM_COILS], l[NUM_COILS];
  uint8_t payload[PCA_NCHAN * 4];
  if (!g_pca_ok || !g_pca_dirty) return;

  uint8_t rot;
  FieldProgram prog;
  __disable_irq();
  memcpy(r, g_pwm_r, sizeof(r));
  memcpy(l, g_pwm_l, sizeof(l));
  rot = g_rotate_active;
  prog = g_field_prog;
  g_pca_dirty = 0;
  __enable_irq();

  if (rot) {
    /* #region agent log */
    if (prog == FIELD_REVOLVE_X) {
      if (r[1] || l[1] || r[3] || l[3]) g_dbg_pair_mis++;
    } else if (prog == FIELD_REVOLVE_Z) {
      if (r[0] || l[0] || r[2] || l[2]) g_dbg_pair_mis++;
    } else if (prog != FIELD_REVOLVE_TILT &&
               (r[0] != r[2] || l[0] != l[2] || r[1] != r[3] || l[1] != l[3])) {
      g_dbg_pair_mis++;
    }
    /* #endregion */
    if (prog == FIELD_REVOLVE_X) {
      /* M1 and M3 are independent (I1 vs I2); M2/M4 off. */
      r[1] = l[1] = r[3] = l[3] = 0;
    } else if (prog == FIELD_REVOLVE_Z) {
      /* M2 and M4 are independent (I1 vs I2); M1/M3 off. */
      r[0] = l[0] = r[2] = l[2] = 0;
    } else if (prog == FIELD_REVOLVE_TILT) {
      /* All four coils independent — do not pair or zero. */
    } else {
      /* Rotate Y: opposite coils share the same signed current. */
      r[2] = r[0];
      l[2] = l[0];
      r[3] = r[1];
      l[3] = l[1];
    }
  }

  for (int i = 0; i < NUM_COILS; i++) {
    pca_pack_chan(&payload[COIL_HW[i].pca_rpwm * 4], r[i]);
    pca_pack_chan(&payload[COIL_HW[i].pca_lpwm * 4], l[i]);
  }
  if (pca_write(0x06, payload, sizeof(payload)) != HAL_OK) {
    g_pca_dirty = 1;
  }
}

static void PCA9685_Init(void)
{
  uint32_t freq = (uint32_t)PWM_TARGET_HZ;
  uint32_t prescale;
  uint8_t zeros[PCA_NCHAN * 4];

  g_pca_ok = 0;
  g_pca_dirty = 0;
  memset(g_pwm_r, 0, sizeof(g_pwm_r));
  memset(g_pwm_l, 0, sizeof(g_pwm_l));

  if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(PCA9685_I2C_ADDR << 1), 3, 20) != HAL_OK) {
    return;
  }

  /* Sleep, set prescale, totem-pole, auto-increment + restart. */
  if (pca_write_u8(0x00, 0x10) != HAL_OK) return;
  if (freq < 24U) freq = 24U;
  if (freq > 1526U) freq = 1526U;
  /* datasheet: prescale = round(osc / (4096 * freq)) - 1 */
  prescale = (PCA9685_OSC_HZ + 2048UL * freq) / (4096UL * freq);
  if (prescale > 0U) prescale -= 1U;
  if (prescale < 3U) prescale = 3U;
  if (prescale > 255U) prescale = 255U;
  if (pca_write_u8(0xFE, (uint8_t)prescale) != HAL_OK) return;
  if (pca_write_u8(0x01, 0x04) != HAL_OK) return; /* MODE2 OUTDRV, OCH=0 (all ch update on STOP) */
  HAL_Delay(1);
  if (pca_write_u8(0x00, 0xA1) != HAL_OK) return; /* RESTART | AI | ALLCALL */
  HAL_Delay(1);

  for (int i = 0; i < PCA_NCHAN; i++) pca_pack_chan(&zeros[i * 4], 0);
  if (pca_write(0x06, zeros, sizeof(zeros)) != HAL_OK) return;
  g_pca_ok = 1;
}

/* TIM6 @ SINE_UPDATE_HZ — open-loop sine sample clock for rotating field */
static void MX_TIM6_Init(void)
{
  __HAL_RCC_TIM6_CLK_ENABLE();

  uint32_t tclk = timer_clock_hz(TIM6); /* 240 MHz with APB1÷2 */
  uint32_t ticks = tclk / SINE_UPDATE_HZ;
  uint32_t psc = 1U;
  uint32_t arr = ticks;
  if (arr > 0xFFFFU) {
    psc = (ticks / 0x10000U) + 1U;
    arr = ticks / psc;
  }
  if (arr < 1U) arr = 1U;

  htim6.Instance = TIM6;
  htim6.Init.Prescaler = psc - 1U;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = arr - 1U;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK) {
    Error_Handler();
  }

  HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

static void MX_ADC1_Init(void)
{
  RCC_PeriphCLKInitTypeDef per = {0};
  GPIO_InitTypeDef g = {0};

  per.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  per.AdcClockSelection = RCC_ADCCLKSOURCE_CLKP;
  if (HAL_RCCEx_PeriphCLKConfig(&per) != HAL_OK) {
    Error_Handler();
  }

  __HAL_RCC_ADC12_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* PA0..PA3 analog for IS (M1–M4) */
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_16B;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc1.Init.OversamplingMode = ENABLE;
  hadc1.Init.Oversampling.Ratio = 16;
  hadc1.Init.Oversampling.RightBitShift = ADC_RIGHTBITSHIFT_4;
  hadc1.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
  hadc1.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) {
    Error_Handler();
  }

  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
    Error_Handler();
  }
}

static void Display_PushStatus(void)
{
  DisplayCoilSnap snap[NUM_COILS];
  for (int i = 0; i < NUM_COILS; i++) {
    snap[i].enabled = coils[i].enabled;
    snap[i].magnitude = g_rotate_active && i < NUM_ROTATE_COILS
                             ? g_rotate_amp_live
                             : coils[i].magnitude;
    snap[i].dir = (coils[i].dir == DIR_FWD) ? 1 : -1;
    snap[i].i_meas_a = coils[i].i_meas_a;
    snap[i].i_limit_a = coils[i].i_limit_a;
  }
  const char *tag = "rotY";
  if (g_field_prog == FIELD_REVOLVE_X) tag = "revX";
  else if (g_field_prog == FIELD_REVOLVE_Z) tag = "revZ";
  else if (g_field_prog == FIELD_REVOLVE_TILT) tag = "tilt";
  DisplayMlxSnap mlx;
  memset(&mlx, 0, sizeof(mlx));
  mlx.present = g_mlx.present;
  mlx.sat = g_mlx_sat;
  mlx.bx_mt = g_mlx_lab_mt[0];
  mlx.by_mt = g_mlx_lab_mt[1];
  mlx.bz_mt = g_mlx_lab_mt[2];
  mlx.babs_mt = g_mlx_babs_mt;
  DisplayUI_Update(g_ctrl_mode == CTRL_OPEN ? "open" : "closed",
                   g_contour_active,
                   g_rotate_active,
                   g_rotate_freq_hz,
                   g_rotate_amp_live,
                   tag,
                   snap, NUM_COILS,
                   &mlx);
}

/* -------------------- coil control -------------------- */
static void Coil_SetPwm(uint8_t idx, uint8_t rpwm, uint8_t lpwm)
{
  if (idx >= NUM_COILS) return;
  if (g_pwm_r[idx] == rpwm && g_pwm_l[idx] == lpwm) return;
  g_pwm_r[idx] = rpwm;
  g_pwm_l[idx] = lpwm;
  g_pca_dirty = 1;
}

static void Coil_ApplyOutputs(uint8_t idx)
{
  if (idx >= NUM_COILS) return;
  /* Rotate ISR owns M1–M4 PWM while active */
  if (g_rotate_active && idx < NUM_ROTATE_COILS) return;

  const CoilHw *hw = &COIL_HW[idx];
  CoilState *st = &coils[idx];

  uint16_t duty = 0;
  if (st->enabled && st->magnitude > 0) {
    duty = st->duty_applied ? st->duty_applied : st->magnitude;
  }

  if (!st->enabled || duty == 0) {
    Coil_SetPwm(idx, 0, 0);
    HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_RESET);
    PCA9685_Flush();
    return;
  }

  HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_SET);
  if (st->dir == DIR_FWD) {
    Coil_SetPwm(idx, (uint8_t)duty, 0);
  } else {
    Coil_SetPwm(idx, 0, (uint8_t)duty);
  }
  PCA9685_Flush();
}

/* True if this coil is driven by the live AC program (not an idle revolve partner). */
static uint8_t field_coil_driven(uint8_t idx)
{
  if (!g_rotate_active || idx >= NUM_COILS) return 0;
  if (g_field_prog == FIELD_REVOLVE_X) return (idx == 0 || idx == 2) ? 1 : 0;
  if (g_field_prog == FIELD_REVOLVE_Z) return (idx == 1 || idx == 3) ? 1 : 0;
  return 1; /* rotate Y uses all four */
}

/* Bipolar H-bridge duty from signed sample (−255..+255). ISR-safe (no I2C). */
static void Coil_ApplySignedDuty(uint8_t idx, int16_t signed_duty)
{
  if (idx >= NUM_COILS) return;
  const CoilHw *hw = &COIL_HW[idx];
  uint16_t mag;

  if (signed_duty > (int16_t)PWM_ARR) signed_duty = (int16_t)PWM_ARR;
  if (signed_duty < -(int16_t)PWM_ARR) signed_duty = -(int16_t)PWM_ARR;

  if (signed_duty >= 0) {
    mag = (uint16_t)signed_duty;
    if (mag == 0) {
      Coil_SetPwm(idx, 0, 0);
      /* AC-driven coils keep EN on through zero so the bridge does not blank
       * each half-cycle (pause). Idle revolve partners still drop EN. */
      if (field_coil_driven(idx)) {
        HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_SET);
      } else {
        HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_RESET);
      }
    } else {
      HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_SET);
      Coil_SetPwm(idx, (uint8_t)mag, 0);
    }
  } else {
    mag = (uint16_t)(-signed_duty);
    HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_SET);
    Coil_SetPwm(idx, 0, (uint8_t)mag);
  }

  coils[idx].duty_applied = mag;
  coils[idx].dir = (signed_duty >= 0) ? DIR_FWD : DIR_REV;
  /* #region agent log */
  g_signed_d[idx] = signed_duty;
  /* #endregion */
}

/* Same signed sample on both coils of a pair (M1∥M3 or M2∥M4). */
static void Coil_ApplyPairedSignedDuty(uint8_t a, uint8_t b, int16_t signed_duty)
{
  Coil_ApplySignedDuty(a, signed_duty);
  Coil_ApplySignedDuty(b, signed_duty);
}

static void Rotate_RecalcPhaseInc(void)
{
  float f = g_rotate_freq_hz;
  if (f < ROTATE_FREQ_MIN_HZ) f = ROTATE_FREQ_MIN_HZ;
  if (f > ROTATE_FREQ_MAX_HZ) f = ROTATE_FREQ_MAX_HZ;
  g_rotate_freq_hz = f;
  /* Advance 2^32 * f / SINE_UPDATE_HZ per tick (full turns in Q32). */
  double inc = ((double)f / (double)SINE_UPDATE_HZ) * 4294967296.0;
  if (inc < 1.0) inc = 1.0;
  if (inc > 4294967295.0) inc = 4294967295.0;
  g_phase_inc = (uint32_t)inc;
}

static const char *rotate_dir_str(RotateDir d)
{
  return d == ROT_CW ? "cw" : "ccw";
}

static const char *field_program_str(FieldProgram p)
{
  if (p == FIELD_REVOLVE_X || p == FIELD_REVOLVE_Z || p == FIELD_REVOLVE_TILT)
    return "revolve";
  return "rotate";
}

static const char *field_axis_str(FieldProgram p)
{
  if (p == FIELD_REVOLVE_X) return "x";
  if (p == FIELD_REVOLVE_Z) return "z";
  if (p == FIELD_REVOLVE_TILT) return "tilt";
  return "y";
}

static uint16_t lut_from_phase(uint32_t ph)
{
  return (uint16_t)(((uint64_t)ph * (uint32_t)SINE_LUT_SIZE) >> 32);
}

static uint16_t lut_mod(uint16_t idx)
{
  return (uint16_t)(idx % (uint16_t)SINE_LUT_SIZE);
}

static uint16_t lut_add(uint16_t idx, int16_t delta)
{
  int32_t v = (int32_t)lut_mod(idx) + (int32_t)delta;
  int32_t n = (int32_t)SINE_LUT_SIZE;
  v %= n;
  if (v < 0) v += n;
  return (uint16_t)v;
}

static int16_t sine_scaled(uint16_t idx, uint8_t amp)
{
  idx = lut_mod(idx);
  return (int16_t)((SINE_LUT[idx] * (int32_t)amp) >> 15);
}

/* Q15 LUT peaks are 1 LSB short of ±1; force ±amp at sin = ±1.
 * Magnitude-first so ±LUT mirrors stay equal (ASR of a negative product is not). */
static int16_t sine_scaled_exact(uint16_t idx, uint8_t amp)
{
  int32_t s, mag;

  idx = lut_mod(idx);
  if (idx == (uint16_t)SINE_LUT_90) return (int16_t)amp;
  if (idx == (uint16_t)(SINE_LUT_90 + 180u)) return (int16_t)(-(int16_t)amp);
  s = SINE_LUT[idx];
  if (s >= 0) return (int16_t)((s * (int32_t)amp) >> 15);
  mag = ((-s) * (int32_t)amp) >> 15;
  return (int16_t)(-mag);
}

static uint8_t clamp_u8_pct(int v)
{
  if (v < (int)REVOLVE_HV_PCT_MIN) return (uint8_t)REVOLVE_HV_PCT_MIN;
  if (v > (int)REVOLVE_HV_PCT_MAX) return (uint8_t)REVOLVE_HV_PCT_MAX;
  return (uint8_t)v;
}

static uint8_t revolve_scaled_amp(uint8_t amp, uint8_t pct)
{
  uint32_t v = ((uint32_t)amp * (uint32_t)pct + 50u) / 100u;
  if (v > 255u) v = 255u;
  return (uint8_t)v;
}

/* Quarter-sine envelope 0→peak while live steps linearly 0→peak. */
static uint8_t revolve_envelope_amp(uint8_t live, uint8_t peak)
{
  int16_t s;
  uint8_t idx;

  if (peak == 0) return 0;
  if (live >= peak) return peak;
  idx = (uint8_t)(((uint32_t)SINE_LUT_90 * (uint32_t)live) / (uint32_t)peak);
  s = sine_scaled(idx, peak);
  if (s < 0) s = 0;
  if (s > 255) s = 255;
  return (uint8_t)s;
}

static int16_t clamp_i16_pwm(float v)
{
  if (v > 255.0f) return 255;
  if (v < -255.0f) return -255;
  if (v >= 0.0f) return (int16_t)(v + 0.5f);
  return (int16_t)(v - 0.5f);
}

/*
 * Revolve pair currents. Ah/Av are lab horizontal/vertical radii in PWM units
 * (amp * h%/100 and amp * v%/100). MLX is display-only and does not enter here.
 *   Bh = Ah·cosθ, Bv = Av·sinθ
 *   I1 = (√2/2)(Av sinθ − Ah cosθ), I2 = −(√2/2)(Ah cosθ + Av sinθ)
 * Equal Ah=Av=A recovers I1=A·sin(θ−π/4), I2=−A·sin(θ+π/4).
 * θ=0 is +Bh. CCW uses −phase.
 */
static void revolve_pair_currents(uint32_t phase, uint8_t amp_h, uint8_t amp_v,
                                  RotateDir dir,
                                  int16_t *i1_out, int16_t *i2_out)
{
  uint32_t ph = phase;
  uint16_t si;
  int16_t i1, i2;

  if (dir == ROT_CCW) ph = 0u - ph;
  si = lut_from_phase(ph); /* 1 LUT step = 1° */

  if (amp_h == amp_v) {
    i1 = sine_scaled_exact(lut_add(si, -(int16_t)SINE_LUT_45), amp_h);
    i2 = (int16_t)(-sine_scaled_exact(lut_add(si, (int16_t)SINE_LUT_45), amp_h));
    if (i1 > 255) i1 = 255;
    if (i1 < -255) i1 = -255;
    if (i2 > 255) i2 = 255;
    if (i2 < -255) i2 = -255;
  } else {
    float invs = 1.0f / 32767.0f;
    float s = (float)SINE_LUT[si] * invs;
    float c = (float)SINE_LUT[lut_add(si, (int16_t)SINE_LUT_90)] * invs;
    float ah = (float)amp_h;
    float av = (float)amp_v;
    float i1f = LAB_COS45 * (av * s - ah * c);
    float i2f = -LAB_COS45 * (ah * c + av * s);
    i1 = clamp_i16_pwm(i1f);
    i2 = clamp_i16_pwm(i2f);
  }
  *i1_out = i1;
  *i2_out = i2;
}

static uint8_t field_is_revolve(FieldProgram p)
{
  return (p == FIELD_REVOLVE_X || p == FIELD_REVOLVE_Z ||
          p == FIELD_REVOLVE_TILT) ? 1 : 0;
}

static uint8_t field_is_tilt(FieldProgram p)
{
  return (p == FIELD_REVOLVE_TILT) ? 1 : 0;
}

static void Tilt_CommitGamma(float deg)
{
  FieldTiltCoeffs c;
  field_tilt_coeffs_from_deg(deg, &c);
  __disable_irq();
  g_tilt_gamma_live = c.deg;
  g_tilt_sin_g = c.sin_g;
  g_tilt_cos_g = c.cos_g;
  __enable_irq();
}

static void Tilt_SetGamma(float deg)
{
  g_tilt_gamma_want = deg;
  /* Idle, or AC running in a non-tilt program: apply immediately. */
  if (!g_rotate_active || !field_is_tilt(g_field_prog)) {
    Tilt_CommitGamma(deg);
  }
}

static float Tilt_PreviewScale(uint8_t amp)
{
  uint8_t ah = revolve_scaled_amp(amp, g_revolve_h_pct);
  uint8_t av = revolve_scaled_amp(amp, g_revolve_v_pct);
  float sg, cg;
  field_tilt_gamma_sincos(g_tilt_gamma_want, &sg, &cg);
  return field_tilt_shared_scale((float)ah, (float)av, sg, cg, (float)PWM_ARR);
}

/*
 * Tilted-axis pair. Direction is applied once by negating phase (same as
 * revolve_pair_currents). Do not also negate H/V or the gamma coefficients.
 */
static void revolve_tilt_currents(uint32_t phase, uint8_t amp_h, uint8_t amp_v,
                                  RotateDir dir, float sin_g, float cos_g,
                                  int16_t out[4], float *scale_out)
{
  uint32_t ph = phase;
  uint16_t si;
  float invs = 1.0f / 32767.0f;
  float s, c, raw[4], scale;

  if (dir == ROT_CCW) ph = 0u - ph;
  si = lut_from_phase(ph);
  s = (float)SINE_LUT[si] * invs;
  c = (float)SINE_LUT[lut_add(si, (int16_t)SINE_LUT_90)] * invs;
  field_tilt_commands((float)amp_h, (float)amp_v, s, c, sin_g, cos_g,
                      (float)PWM_ARR, raw, &scale);
  field_tilt_quantize(raw, out, (int16_t)PWM_ARR);
  if (scale_out) *scale_out = scale;
}

static uint32_t revolve_corner_phase(uint8_t q, RotateDir dir)
{
  uint32_t ph = ((uint32_t)(q & 3u)) << 30;
  if (dir == ROT_CCW) ph = 0u - ph;
  return ph;
}

static void Revolve_ClearPark(void)
{
  g_revolve_park = 0;
  g_revolve_travel = 0;
}

static void Revolve_GotoCorner(uint8_t q)
{
  FieldProgram p = g_field_prog_want;
  uint32_t tgt;

  q &= 3u;
  if (!field_is_revolve(p)) {
    if (field_is_revolve(g_field_prog)) p = g_field_prog;
    else p = FIELD_REVOLVE_Z;
    Field_SetProgram(p);
  }

  g_revolve_park = 1;
  g_revolve_corner = q;
  tgt = revolve_corner_phase(q, g_rotate_dir);

  if (!g_rotate_active) {
    Field_Start(p);
    g_phase = tgt;
    g_revolve_travel = 0;
    g_revolve_phase_hold = 1;
    g_revolve_env_peak = g_rotate_amp;
    return;
  }

  /* Program fade: RampUpdate plants phase at the park corner when it applies. */
  if (!field_is_revolve(g_field_prog) || g_field_prog != p) {
    g_revolve_travel = 0;
    return;
  }

  if (g_phase == tgt) {
    g_revolve_travel = 0;
    g_revolve_phase_hold = 1;
    return;
  }

  /* Already at amp: sine-travel along the circle. Still swelling: jump
   * the frozen phase to the new corner (amp is small). */
  if (g_revolve_phase_hold && g_rotate_amp_live != g_rotate_amp) {
    g_phase = tgt;
    g_revolve_travel = 0;
    return;
  }

  g_revolve_travel = 1;
  g_revolve_phase_hold = 0;
}

/* #region agent log */
static void dbg_print_snapshot(const char *where)
{
  uint8_t pr[NUM_COILS], pl[NUM_COILS];
  uint32_t isr, ore1, empty, lines;
  uint8_t over, pca, rot, amp, live;
  __disable_irq();
  memcpy(pr, g_pwm_r, sizeof(pr));
  memcpy(pl, g_pwm_l, sizeof(pl));
  isr = g_dbg_isr;
  ore1 = g_dbg_ore1;
  empty = g_dbg_empty_cr;
  lines = g_dbg_cli_lines;
  over = g_dbg_over;
  pca = g_pca_ok;
  rot = g_rotate_active;
  amp = g_rotate_amp;
  live = g_rotate_amp_live;
  __enable_irq();
  uart_printf(
      "DBG %s rot=%u axis=%s amp=%u live=%u over=%u pca=%u isr=%lu ore1=%lu empty=%lu lines=%lu "
      "pr=%u,%u,%u,%u pl=%u,%u,%u,%u sd=%d,%d,%d,%d pmis=%lu "
      "I=%.2f,%.2f,%.2f,%.2f off=%.2f,%.2f,%.2f,%.2f\r\n",
      where ? where : "?", (unsigned)rot, field_axis_str(g_field_prog),
      (unsigned)amp, (unsigned)live, (unsigned)over, (unsigned)pca,
      (unsigned long)isr, (unsigned long)ore1, (unsigned long)empty,
      (unsigned long)lines,
      (unsigned)pr[0], (unsigned)pr[1], (unsigned)pr[2], (unsigned)pr[3],
      (unsigned)pl[0], (unsigned)pl[1], (unsigned)pl[2], (unsigned)pl[3],
      (int)g_signed_d[0], (int)g_signed_d[1], (int)g_signed_d[2], (int)g_signed_d[3],
      (unsigned long)g_dbg_pair_mis,
      (double)coils[0].i_meas_a, (double)coils[1].i_meas_a,
      (double)coils[2].i_meas_a, (double)coils[3].i_meas_a,
      (double)g_i_offset_a[0], (double)g_i_offset_a[1],
      (double)g_i_offset_a[2], (double)g_i_offset_a[3]);
}
/* #endregion */

static uint8_t rotate_step_toward(uint8_t cur, uint8_t tgt)
{
  if (cur == tgt) return cur;
  uint8_t step = (uint8_t)ROTATE_RAMP_STEP;
  if (step < 1u) step = 1u;
  if (cur < tgt) {
    uint8_t d = (uint8_t)(tgt - cur);
    return (uint8_t)(cur + (d < step ? d : step));
  }
  {
    uint8_t d = (uint8_t)(cur - tgt);
    return (uint8_t)(cur - (d < step ? d : step));
  }
}

static void Rotate_Stop(void)
{
  g_rotate_active = 0;
  g_rotate_stop_pending = 0;
  g_contour_active = 0; /* idle header/json must not keep a stale contour flag */
  HAL_TIM_Base_Stop_IT(&htim6);
  g_rotate_amp_live = 0;
  g_rotate_oc_streak = 0;
  g_revolve_phase_hold = 0;
  g_revolve_env_peak = 0;
  Revolve_ClearPark();
  g_rotate_dir = g_rotate_dir_want;
  g_field_prog = g_field_prog_want;

  int n = (NUM_COILS < NUM_ROTATE_COILS) ? NUM_COILS : NUM_ROTATE_COILS;
  for (int i = 0; i < n; i++) {
    coils[i].enabled = 0;
    coils[i].duty_applied = 0;
    coils[i].pi_integral = 0;
    Coil_ApplyOutputs((uint8_t)i);
  }
}

static void Rotate_RequestStop(void)
{
  if (!g_rotate_active) {
    Rotate_Stop();
    return;
  }
  g_rotate_stop_pending = 1;
}

static void Rotate_SetDir(RotateDir d)
{
  g_rotate_dir_want = d;
  /* Idle: apply immediately. While rotating, ramp to 0, flip, ramp back up. */
  if (!g_rotate_active) g_rotate_dir = d;
}

static void Field_SetProgram(FieldProgram p)
{
  g_field_prog_want = p;
  if (!g_rotate_active) g_field_prog = p;
}

static void Field_ApplyOptionalFreqAmp(int argc, char **argv, int start)
{
  if (start < argc && argv[start] && argv[start][0]) {
    float f = (float)atof(argv[start]);
    if (f < ROTATE_FREQ_MIN_HZ) f = ROTATE_FREQ_MIN_HZ;
    if (f > ROTATE_FREQ_MAX_HZ) f = ROTATE_FREQ_MAX_HZ;
    g_rotate_freq_hz = f;
    Rotate_RecalcPhaseInc();
  }
  if ((start + 1) < argc && argv[start + 1] && argv[start + 1][0]) {
    int a = atoi(argv[start + 1]);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    g_rotate_amp = (uint8_t)a;
    for (int i = 0; i < NUM_ROTATE_COILS && i < NUM_COILS; i++) {
      coils[i].magnitude = g_rotate_amp;
    }
  }
}

static void Field_Start(FieldProgram p)
{
#if (NUM_COILS < NUM_ROTATE_COILS)
  uart_write("field needs NUM_COILS>=4\r\n");
  return;
#else
  g_contour_active = 0;
  g_rotate_stop_pending = 0;
  g_field_prog_want = p;
  Rotate_RecalcPhaseInc();

  if (g_rotate_active) {
    /* Already running — cancel stop; ramp switches program if needed. */
    return;
  }

  if (g_revolve_park && field_is_revolve(g_field_prog_want)) {
    g_phase = revolve_corner_phase(g_revolve_corner, g_rotate_dir_want);
  } else {
    g_phase = 0;
    Revolve_ClearPark();
  }
  g_rotate_amp_live = 0; /* fade in from zero */
  g_rotate_dir = g_rotate_dir_want;
  g_field_prog = g_field_prog_want;
  if (field_is_tilt(g_field_prog)) {
    Tilt_CommitGamma(g_tilt_gamma_want);
  }
  g_revolve_phase_hold = field_is_revolve(g_field_prog) ? 1 : 0;
  g_revolve_env_peak = g_rotate_amp;

  for (int i = 0; i < NUM_ROTATE_COILS; i++) {
    coils[i].enabled = 1;
    coils[i].pi_integral = 0;
    coils[i].magnitude = g_rotate_amp;
  }

  g_rotate_active = 1;
  HAL_TIM_Base_Start_IT(&htim6);
#endif
}

static void Rotate_RampUpdate(void)
{
  if (!g_rotate_active) return;

  uint8_t prog_mismatch = (g_field_prog != g_field_prog_want);
  uint8_t dir_mismatch = (g_rotate_dir != g_rotate_dir_want);
  uint8_t gamma_mismatch = 0;
  if (field_is_tilt(g_field_prog) || field_is_tilt(g_field_prog_want)) {
    gamma_mismatch = (g_tilt_gamma_live != g_tilt_gamma_want) ? 1 : 0;
  }

  if ((dir_mismatch || prog_mismatch) && g_rotate_amp_live == 0 &&
      !g_rotate_stop_pending) {
    g_rotate_dir = g_rotate_dir_want;
    g_field_prog = g_field_prog_want;
    dir_mismatch = 0;
    prog_mismatch = 0;
    if (field_is_tilt(g_field_prog)) {
      Tilt_CommitGamma(g_tilt_gamma_want);
      gamma_mismatch = 0;
    }
    if (field_is_revolve(g_field_prog)) {
      if (g_revolve_park) {
        g_phase = revolve_corner_phase(g_revolve_corner, g_rotate_dir);
        g_revolve_travel = 0;
      } else {
        g_phase = 0;
      }
      g_revolve_phase_hold = 1;
      g_revolve_env_peak = g_rotate_amp;
    } else {
      g_revolve_phase_hold = 0;
      Revolve_ClearPark();
    }
  }

  /* Gamma-only: ramp through 0, commit sin/cos together, keep phase. */
  if (gamma_mismatch && g_rotate_amp_live == 0 && !g_rotate_stop_pending) {
    Tilt_CommitGamma(g_tilt_gamma_want);
    gamma_mismatch = 0;
    if (field_is_revolve(g_field_prog)) {
      g_revolve_phase_hold = 1;
      g_revolve_env_peak = g_rotate_amp;
    }
  }

  uint8_t target = g_rotate_amp;
  if (g_rotate_stop_pending || dir_mismatch || prog_mismatch || gamma_mismatch) {
    target = 0;
  }

  uint8_t over_now = 0;
  for (uint8_t i = 0; i < NUM_ROTATE_COILS && i < NUM_COILS; i++) {
    /* Idle coils (duty 0) must not trip the shared amp clamp — IS can sit
     * at a large offset while PWM is off (seen ~3.5 A at duty 0–1). */
    if (coils[i].duty_applied > 0 &&
        coils[i].i_limit_a > 0.0f && coils[i].i_meas_a > coils[i].i_limit_a) {
      over_now = 1;
      break;
    }
  }
  if (over_now) {
    if (g_rotate_oc_streak < 255u) g_rotate_oc_streak++;
  } else {
    g_rotate_oc_streak = 0;
  }
  uint8_t over = (g_rotate_oc_streak >= (uint8_t)ROTATE_OC_HYST_TICKS) ? 1 : 0;
  /* #region agent log */
  g_dbg_over = over;
  /* #endregion */

  if (over && g_rotate_amp_live > 0) {
    /* One step down per tick — don't also ramp up into the limit. */
    if (g_rotate_amp_live > target) {
      g_rotate_amp_live = rotate_step_toward(g_rotate_amp_live, target);
    } else if (g_rotate_amp_live > 0) {
      g_rotate_amp_live--;
    }
  } else {
    g_rotate_amp_live = rotate_step_toward(g_rotate_amp_live, target);
  }

  {
    uint8_t is_rev = field_is_revolve(g_field_prog);
    if (!is_rev) {
      g_revolve_phase_hold = 0;
    } else if (g_rotate_stop_pending || dir_mismatch || prog_mismatch ||
               gamma_mismatch) {
      if (!g_revolve_phase_hold) {
        g_revolve_env_peak = g_rotate_amp_live;
      }
      g_revolve_phase_hold = 1;
    } else if (g_revolve_park && !g_revolve_travel) {
      g_revolve_phase_hold = 1;
    } else if (g_rotate_amp_live == target && !g_revolve_travel) {
      g_revolve_phase_hold = 0;
    }
  }

  if (g_rotate_stop_pending && g_rotate_amp_live == 0) {
    Rotate_Stop();
  }
}

static void Rotate_StopIfDcConflict(uint8_t idx)
{
  if (g_rotate_active && idx < NUM_ROTATE_COILS) {
    Rotate_Stop();
  }
}

static void Coil_AllOff(void)
{
  Rotate_Stop();
  g_contour_active = 0;
  for (int i = 0; i < NUM_COILS; i++) {
    coils[i].enabled = 0;
    coils[i].duty_applied = 0;
    coils[i].pi_integral = 0;
    Coil_ApplyOutputs((uint8_t)i);
  }
}

static void Coil_SetEnabled(uint8_t idx, uint8_t on)
{
  if (idx >= NUM_COILS) return;
  Rotate_StopIfDcConflict(idx);
  coils[idx].enabled = on ? 1 : 0;
  if (!on) {
    coils[idx].duty_applied = 0;
    coils[idx].pi_integral = 0;
  }
  Coil_ApplyOutputs(idx);
}

static void Coil_SetMagnitude(uint8_t idx, uint8_t mag)
{
  if (idx >= NUM_COILS) return;
  Rotate_StopIfDcConflict(idx);
  coils[idx].magnitude = mag;
  if (g_ctrl_mode == CTRL_OPEN) {
    coils[idx].duty_applied = mag;
  }
  Coil_ApplyOutputs(idx);
}

static void Coil_SetDir(uint8_t idx, CoilDir d)
{
  if (idx >= NUM_COILS) return;
  Rotate_StopIfDcConflict(idx);
  coils[idx].dir = d;
  Coil_ApplyOutputs(idx);
}

/* -------------------- current sense -------------------- */
static float Sense_ReadAmpsRaw(uint8_t idx)
{
  if (idx >= NUM_COILS) return 0.0f;

  ADC_ChannelConfTypeDef cfg = {0};
  cfg.Channel = COIL_HW[idx].adc_channel;
  cfg.Rank = ADC_REGULAR_RANK_1;
  cfg.SamplingTime = ADC_SAMPLETIME_387CYCLES_5;
  cfg.SingleDiff = ADC_SINGLE_ENDED;
  cfg.OffsetNumber = ADC_OFFSET_NONE;
  cfg.Offset = 0;
  cfg.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK) {
    return 0.0f;
  }

  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 2) != HAL_OK) {
    HAL_ADC_Stop(&hadc1);
    return 0.0f;
  }
  uint32_t raw = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);

  float vis = ((float)raw / ADC_MAX_COUNTS) * ADC_VREF;
  return vis * (IS_K_RATIO / IS_RIS_OHM);
}

static float Sense_ReadAmps(uint8_t idx)
{
  if (idx >= NUM_COILS) return 0.0f;
  float i = Sense_ReadAmpsRaw(idx) - g_i_offset_a[idx];
  if (i < 0.0f) i = 0.0f;
  coils[idx].i_meas_a = i;
  return i;
}

static void Sense_CalibrateOffsets(void)
{
  for (int i = 0; i < NUM_COILS; i++) {
    HAL_GPIO_WritePin(COIL_HW[i].en_port, COIL_HW[i].en_pin, GPIO_PIN_RESET);
    Coil_SetPwm((uint8_t)i, 0, 0);
  }
  PCA9685_Flush();
  HAL_Delay(5);
  for (int i = 0; i < NUM_COILS; i++) {
    float s = 0.0f;
    for (int n = 0; n < 8; n++) {
      s += Sense_ReadAmpsRaw((uint8_t)i);
    }
    g_i_offset_a[i] = s / 8.0f;
    coils[i].i_meas_a = 0.0f;
  }
}

static void CurrentLoop_Update(void)
{
  if (g_rotate_active) {
    /* One ADC channel per tick — keeps main free for PCA9685_Flush.
     * Soft-limit uses the freshest reading for each coil as it rotates. */
    uint8_t i = g_field_sense_rr % (uint8_t)NUM_ROTATE_COILS;
    g_field_sense_rr = (uint8_t)((i + 1U) % (uint8_t)NUM_ROTATE_COILS);
    Sense_ReadAmps(i);
    PCA9685_Flush();
    Rotate_RampUpdate();
    return;
  }

  for (uint8_t i = 0; i < NUM_COILS; i++) {
    CoilState *st = &coils[i];

    if (!st->enabled) {
      st->duty_applied = 0;
      st->pi_integral = 0;
      continue;
    }

    /* Open-loop: skip ADC unless a soft current limit is armed */
    if (g_ctrl_mode == CTRL_OPEN && st->i_limit_a <= 0.0f) {
      st->duty_applied = st->magnitude;
      Coil_ApplyOutputs(i);
      continue;
    }

    float i_meas = Sense_ReadAmps(i);
    float i_ref = ((float)st->magnitude / 255.0f) * I_FULLSCALE_A;

    /* Soft over-current clamp regardless of mode */
    if (st->i_limit_a > 0.0f && i_meas > st->i_limit_a) {
      if (st->duty_applied > 0) st->duty_applied--;
      Coil_ApplyOutputs(i);
      continue;
    }

    if (g_ctrl_mode == CTRL_OPEN) {
      st->duty_applied = st->magnitude;
      Coil_ApplyOutputs(i);
      continue;
    }

    /* Simple PI toward i_ref (closed-loop current) */
    const float Kp = 40.0f;
    const float Ki = 800.0f;
    float dt = 1.0f / (float)CURRENT_LOOP_HZ;
    float err = i_ref - i_meas;
    st->pi_integral += err * dt;
    if (st->pi_integral > 2.0f) st->pi_integral = 2.0f;
    if (st->pi_integral < -2.0f) st->pi_integral = -2.0f;

    float u = Kp * err + Ki * st->pi_integral;
    if (u < 0.0f) u = 0.0f;
    if (u > 255.0f) u = 255.0f;
    st->duty_applied = (uint16_t)(u + 0.5f);
    Coil_ApplyOutputs(i);
  }

  Rotate_RampUpdate();
}

/* -------------------- CLI -------------------- */
static int parse_motor_token(const char *tok, uint8_t *idx_out)
{
  if (!tok || !tok[0]) return -1;
  if ((tok[0] == 'M' || tok[0] == 'm') && isdigit((unsigned char)tok[1])) {
    int n = atoi(tok + 1);
    if (n >= 1 && n <= NUM_COILS) {
      *idx_out = (uint8_t)(n - 1);
      return 0;
    }
  }
  if (isdigit((unsigned char)tok[0])) {
    int n = atoi(tok);
    if (n >= 1 && n <= NUM_COILS) {
      *idx_out = (uint8_t)(n - 1);
      return 0;
    }
  }
  return -1;
}

static void Mlx_ApplySample(const MlxSample *s)
{
  float chip_ut[3];
  float lab_mt[3];
  int i;

  if (!s) return;
  chip_ut[0] = s->ut_x;
  chip_ut[1] = s->ut_y;
  chip_ut[2] = s->ut_z;
  for (i = 0; i < 3; i++) {
    uint8_t src = g_mlx_src[i];
    if (src > 2u) src = 0;
    lab_mt[i] = 0.001f * (float)g_mlx_sgn[i] * chip_ut[src];
    if (g_mlx_b0_ok) lab_mt[i] -= g_mlx_b0_mt[i];
  }
  g_mlx_lab_mt[0] = lab_mt[0];
  g_mlx_lab_mt[1] = lab_mt[1];
  g_mlx_lab_mt[2] = lab_mt[2];
  g_mlx_babs_mt = sqrtf(lab_mt[0] * lab_mt[0] + lab_mt[1] * lab_mt[1] +
                        lab_mt[2] * lab_mt[2]);
  g_mlx_sat = s->sat;
}

static uint8_t mlx_parse_axis_tok(const char *t, uint8_t *src, int8_t *sgn)
{
  const char *p = t;
  int8_t s = 1;
  if (!t || !t[0]) return 0;
  if (*p == '+') p++;
  else if (*p == '-') { s = -1; p++; }
  if (p[0] == 'x' || p[0] == 'X') *src = 0;
  else if (p[0] == 'y' || p[0] == 'Y') *src = 1;
  else if (p[0] == 'z' || p[0] == 'Z') *src = 2;
  else return 0;
  if (p[1] != '\0') return 0;
  *sgn = s;
  return 1;
}

static uint8_t mlx_avg_lab(int n, float lab_mt[3])
{
  float acc[3] = {0, 0, 0};
  int i, got = 0;
  MlxSample s;
  if (!g_mlx.present) return 0;
  for (i = 0; i < n; i++) {
    if (!MLX90393_ReadBlocking(&g_mlx, &s)) continue;
    {
      float chip[3] = {s.ut_x, s.ut_y, s.ut_z};
      int a;
      for (a = 0; a < 3; a++) {
        uint8_t src = g_mlx_src[a];
        if (src > 2u) src = 0;
        acc[a] += 0.001f * (float)g_mlx_sgn[a] * chip[src];
      }
    }
    g_mlx_sat = (uint8_t)(g_mlx_sat | s.sat);
    got++;
    PCA9685_Flush();
  }
  if (got == 0) return 0;
  lab_mt[0] = acc[0] / (float)got;
  lab_mt[1] = acc[1] / (float)got;
  lab_mt[2] = acc[2] / (float)got;
  if (g_mlx_b0_ok) {
    lab_mt[0] -= g_mlx_b0_mt[0];
    lab_mt[1] -= g_mlx_b0_mt[1];
    lab_mt[2] -= g_mlx_b0_mt[2];
  }
  return 1;
}

static void mlx_all_coils_off(void)
{
  int i;
  for (i = 0; i < NUM_COILS; i++) {
    coils[i].enabled = 0;
    Coil_ApplySignedDuty((uint8_t)i, 0);
  }
  PCA9685_Flush();
}

static void Mlx_PrintStatus(void)
{
  uart_printf("mlx %s addr=0x%02X poll=%u sat=%u map=%c%c %c%c %c%c b0=%s\r\n",
              g_mlx.present ? "ok" : "MISSING",
              (unsigned)g_mlx.addr,
              (unsigned)g_mlx_en,
              (unsigned)g_mlx_sat,
              g_mlx_sgn[0] < 0 ? '-' : '+', "xyz"[g_mlx_src[0]],
              g_mlx_sgn[1] < 0 ? '-' : '+', "xyz"[g_mlx_src[1]],
              g_mlx_sgn[2] < 0 ? '-' : '+', "xyz"[g_mlx_src[2]],
              g_mlx_b0_ok ? "set" : "off");
  if (g_mlx.present) {
    uart_printf("  B_mT Bx=%.3f By=%.3f Bz=%.3f |B|=%.3f\r\n",
                (double)g_mlx_lab_mt[0], (double)g_mlx_lab_mt[1],
                (double)g_mlx_lab_mt[2], (double)g_mlx_babs_mt);
    if (g_mlx_b0_ok) {
      uart_printf("  B0_mT %.3f %.3f %.3f\r\n",
                  (double)g_mlx_b0_mt[0], (double)g_mlx_b0_mt[1],
                  (double)g_mlx_b0_mt[2]);
    }
  }
}

static uint8_t mlx_take_offset(void)
{
  float raw[3];
  mlx_all_coils_off();
  HAL_Delay(MLX_CAL_SETTLE_MS);
  g_mlx_b0_ok = 0;
  g_mlx_sat = 0;
  if (!mlx_avg_lab((int)MLX_CAL_AVG, raw)) return 0;
  g_mlx_b0_mt[0] = raw[0];
  g_mlx_b0_mt[1] = raw[1];
  g_mlx_b0_mt[2] = raw[2];
  g_mlx_b0_ok = 1;
  return 1;
}

static void mlx_try_init(void)
{
  uint8_t found[4];
  uint8_t n = MLX90393_Scan(&hi2c2, found, 4);
  uint8_t addr = found[0];
  if (n == 0) {
    /* Probe the two common breakout bases even if ACK was flaky. */
    if (MLX90393_Init(&g_mlx, &hi2c2, 0x18u)) return;
    if (MLX90393_Init(&g_mlx, &hi2c2, 0x0Cu)) return;
    g_mlx.present = 0;
    return;
  }
  (void)MLX90393_Init(&g_mlx, &hi2c2, addr);
}

static void CLI_PrintHelp(void)
{
  uart_write(
    "\r\n=== H743 BTS7960 coil driver ===\r\n"
    "help                         show this help\r\n"
    "status                       show coil states + currents\r\n"
    "sense                        sample IS currents once\r\n"
    "on <M1..M4> [..]             enable coil(s) [DC]\r\n"
    "off <M1..M4|all> [..]        disable coil(s)\r\n"
    "set <M#|all> <0..255>        magnitude (duty / Iref scale) [DC]\r\n"
    "dir <M#|all> <+|->           B-field / current direction [DC]\r\n"
    "drive <M#> <0..255> <+|->    set mag+dir and enable [DC]\r\n"
    "limit <M#|all> <0..255>      soft current limit (255=~2A, 0=off)\r\n"
    "mode open|closed             open-loop duty vs IS closed-loop [DC]\r\n"
    "rotate on|off [Hz] [0..255]  Y-axis rotate in XZ (M1=M3 cos, M2=M4 sin)\r\n"
    "revolve x|z|tilt [Hz] [amp]  x=YZ M1/M3; z=XY M2/M4; tilt=fixed XZ axis, all four\r\n"
    "revolve off                  stop AC field (same ramp as rotate off)\r\n"
    "axis y|x|z|tilt              select program without start (y=rotate, x/z/tilt=revolve)\r\n"
    "gamma <0..90>                tilt-axis angle in deg from +X toward +Z (tilt mode)\r\n"
    "freq <Hz>                    AC field freq 0.01..50 Hz\r\n"
    "amp <0..255>                 shared AC amp (revolve: lab |B|; rotate Y: pair peak)\r\n"
    "rev_hv <h%> <v%>             Ah=amp*h/100 Av=amp*v/100 (100 100=circle; 25 100 cuts H 4x)\r\n"
    "rot_dir cw|ccw               rotate: sign of sin pair; revolve: omega -> -omega\r\n"
    "corner Q0|Q1|Q2|Q3|run       park revolve at a cardinal, or resume stepping\r\n"
    "contour on|off               enable M1..M4 together (DC contour)\r\n"
    "program P1|P2|stop           preset patterns\r\n"
    "  P1 = M1..M4 ON (keep mag/dir)\r\n"
    "  P2 = M1..M4 ON @128 FWD\r\n"
    "  stop = all off\r\n"
    "json                         machine-readable status (ESP32 / UI)\r\n"
    "teleplot on|off              stream PWM B + measured Bx_m/By_m/Bz_m/Babs_m on USART1 (~50 Hz)\r\n"
    "mlx                          show measured B (mT); display only, never drives coils\r\n"
    "mlx on|off                   poll the MLX90393 (I2C2)\r\n"
    "mlx scan                     rescan I2C2 0x0C..0x1B\r\n"
    "mlx map <labX> <labY> <labZ> chip axes, e.g. x y z  or  x -z y\r\n"
    "mlx offset                   coils off → Earth/bias B0 (subtracted from display)\r\n"
    "ping                         bridge heartbeat (replies PONG)\r\n"
    "\r\n"
    "Y is vertical (out of plane); X and Z are horizontal. Coils tilted ~45 deg.\r\n"
    "rotate Y: all four, pairs same direction, 90 deg apart — field in XZ.\r\n"
    "revolve z: M2=I1, M4=I2; Ah/Av from rev_hv (equal=circle). Q0 uses Ah, Q1 uses Av.\r\n"
    "revolve x: M1=I1, M3=I2, same Ah/Av ellipse; M2,M4 off — field in YZ.\r\n"
    "revolve tilt: n=(cos g,0,sin g) in XZ; all four coils. gamma 0=+X (YZ plane), 90=+Z (XY).\r\n"
    "  Endpoint fields match revolve X/Z planes; coil commands differ (equal-V split).\r\n"
    "  Unequal rev_hv is an ellipse, not constant |B|. Shared scale if a coil would exceed 255.\r\n"
    "  CW = +theta (omega = -n). CCW = -theta (omega = +n). Direction applied once.\r\n"
    "  gamma=90 Q0 is -X (not legacy revolve-Z +X). Change gamma while running ramps through 0.\r\n"
    "CW corners: Q0=+h/+Bh, Q1=+Y/+Bv, Q2=-h/-Bh, Q3=-Y/-Bv. 'corner Qn' parks there.\r\n"
    "Revolve start/stop: freeze phase, quarter-sine envelope on |A|, then step.\r\n"
    "Start/stop/CW↔CCW/program change fade live amp through 0 (~1.3 s 0↔255).\r\n"
    "AC field: driven-coil EN stays on through zero; PCA flush continues during json/status.\r\n"
    "off all cuts immediately. DC writes to M1..M4 auto-stop the AC field.\r\n"
    "Limits while the field is on derate shared amp (3-tick hysteresis). Boot soft-limits ~2 A.\r\n"
    "PWM is PCA9685 on I2C1 PB8=SCL PB9=SDA (addr 0x40, ~1 kHz). EN is STM32 GPIO.\r\n"
    "MLX90393 on I2C2 PB10=SCL PB11=SDA (3.3 V, CS high). Do not share I2C1.\r\n"
    "teleplot: USART1 Serial Monitor only (not ESP32). Open Teleplot on that port.\r\n"
    "  PWM-proxy B (45 deg, signed duty as current): Bx By Bz Babs, Bxy/Bxz/Byz xy.\r\n"
    "  Measured MLX (mT, B0 subtracted): Bx_m By_m Bz_m Babs_m, Bxy_m/Byz_m xy.\r\n"
    "  Coil PWM is geometric (rev_hv / 45 deg). MLX is display-only.\r\n"
    "\r\n"
  );
}

static void CLI_PrintStatus(void)
{
  uart_printf("mode=%s  contour=%u  field=%u  program=%s  axis=%s  freq=%.2f Hz  amp=%u  amp_live=%u  rot_dir=%s\r\n",
              g_ctrl_mode == CTRL_OPEN ? "open" : "closed",
              (unsigned)g_contour_active,
              (unsigned)g_rotate_active,
              field_program_str(g_field_prog_want),
              field_axis_str(g_field_prog_want),
              (double)g_rotate_freq_hz,
              (unsigned)g_rotate_amp,
              (unsigned)g_rotate_amp_live,
              rotate_dir_str(g_rotate_dir_want));
  uart_printf("rev_hv h=%u%% v=%u%%  (Ah=amp*h/100, Av=amp*v/100; unequal = ellipse)\r\n",
              (unsigned)g_revolve_h_pct, (unsigned)g_revolve_v_pct);
  {
    float sc = Tilt_PreviewScale(g_rotate_amp);
    uart_printf("gamma=%.2f deg (live=%.2f)  tilt_scale=%.4f%s\r\n",
                (double)g_tilt_gamma_want, (double)g_tilt_gamma_live,
                (double)sc, sc < 1.0f ? " (limited)" : "");
  }
  if (g_revolve_park) {
    uart_printf("corner=Q%u%s\r\n", (unsigned)g_revolve_corner,
                g_revolve_travel ? " (traveling)" : " (parked)");
  } else {
    uart_write("corner=run\r\n");
  }
  uart_printf("pwm=%u Hz pca9685  sine_isr=%u Hz  coils=%d  pca=%s\r\n",
              (unsigned)PWM_TARGET_HZ, (unsigned)SINE_UPDATE_HZ, NUM_COILS,
              g_pca_ok ? "ok" : "MISSING");
  uart_printf("pairs M1/M3 signed=%d/%d  M2/M4 signed=%d/%d  mismatch=%lu\r\n",
              (int)g_signed_d[0], (int)g_signed_d[2],
              (int)g_signed_d[1], (int)g_signed_d[3],
              (unsigned long)g_dbg_pair_mis);
  Mlx_PrintStatus();
  for (int i = 0; i < NUM_COILS; i++) {
    CoilState *s = &coils[i];
    uart_printf("M%d en=%u mag=%3u dir=%s duty=%3u I=%.3fA lim=%.2fA\r\n",
                i + 1, s->enabled, s->magnitude,
                s->dir == DIR_FWD ? "+" : "-",
                s->duty_applied, s->i_meas_a, s->i_limit_a);
  }
  /* #region agent log */
  dbg_print_snapshot("status");
  /* #endregion */
}

static void CLI_HandleLine(char *line)
{
  /* tokenize */
  char *argv[16];
  int argc = 0;
  for (char *p = strtok(line, " \t\r\n"); p && argc < 16; p = strtok(NULL, " \t\r\n")) {
    argv[argc++] = p;
  }
  if (argc == 0) return;

  for (char *c = argv[0]; *c; c++) *c = (char)tolower((unsigned char)*c);
  const char *cmd = argv[0];

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    CLI_PrintHelp();
    return;
  }
  if (!strcmp(cmd, "ping")) {
    uart_write("PONG\r\n");
    /* #region agent log */
    dbg_print_snapshot("ping");
    /* #endregion */
    return;
  }
  if (!strcmp(cmd, "json")) {
    /* Sample ADCs first, then build one buffer and TX continuously. */
    float amps[NUM_COILS];
    for (int i = 0; i < NUM_COILS; i++) {
      amps[i] = Sense_ReadAmps((uint8_t)i);
      if (g_rotate_active) PCA9685_Flush();
    }
    static char js[2048];
    uint8_t pr[NUM_COILS], pl[NUM_COILS];
    int16_t sd[NUM_COILS];
    uint8_t pca, dirty, over;
    uint32_t ore1, ore3, empty, lines, isr;
    /* #region agent log */
    __disable_irq();
    memcpy(pr, g_pwm_r, sizeof(pr));
    memcpy(pl, g_pwm_l, sizeof(pl));
    memcpy(sd, (const void *)g_signed_d, sizeof(sd));
    pca = g_pca_ok;
    dirty = g_pca_dirty;
    over = g_dbg_over;
    ore1 = g_dbg_ore1;
    ore3 = g_dbg_ore3;
    empty = g_dbg_empty_cr;
    lines = g_dbg_cli_lines;
    isr = g_dbg_isr;
    __enable_irq();
    /* #endregion */
    int n = snprintf(js, sizeof(js),
                     "{\"ok\":1,\"mode\":\"%s\",\"contour\":%u,\"rotate\":%u,"
                     "\"program\":\"%s\",\"program_live\":\"%s\","
                     "\"axis\":\"%s\",\"axis_live\":\"%s\","
                     "\"freq\":%.2f,\"amp\":%u,\"amp_live\":%u,"
                     "\"rev_h\":%u,\"rev_v\":%u,"
                     "\"gamma\":%.2f,\"gamma_live\":%.2f,\"tilt_scale\":%.4f,"
                     "\"rot_dir\":\"%s\",\"rot_dir_live\":\"%s\",\"n\":%d,"
                     "\"corner\":%d,\"park\":%u,\"travel\":%u,"
                     "\"signed\":[%d,%d,%d,%d],"
                     "\"pr\":[%u,%u,%u,%u],\"pl\":[%u,%u,%u,%u],\"coils\":[",
                     g_ctrl_mode == CTRL_OPEN ? "open" : "closed",
                     (unsigned)g_contour_active,
                     (unsigned)g_rotate_active,
                     field_program_str(g_field_prog_want),
                     field_program_str(g_field_prog),
                     field_axis_str(g_field_prog_want),
                     field_axis_str(g_field_prog),
                     (double)g_rotate_freq_hz,
                     (unsigned)g_rotate_amp,
                     (unsigned)g_rotate_amp_live,
                     (unsigned)g_revolve_h_pct,
                     (unsigned)g_revolve_v_pct,
                     (double)g_tilt_gamma_want,
                     (double)g_tilt_gamma_live,
                     (double)g_tilt_scale,
                     rotate_dir_str(g_rotate_dir_want),
                     rotate_dir_str(g_rotate_dir),
                     NUM_COILS,
                     g_revolve_park ? (int)g_revolve_corner : -1,
                     (unsigned)g_revolve_park,
                     (unsigned)g_revolve_travel,
                     (int)sd[0], (int)sd[1], (int)sd[2], (int)sd[3],
                     (unsigned)pr[0], (unsigned)pr[1], (unsigned)pr[2], (unsigned)pr[3],
                     (unsigned)pl[0], (unsigned)pl[1], (unsigned)pl[2], (unsigned)pl[3]);
    for (int i = 0; i < NUM_COILS && n > 0 && n < (int)sizeof(js) - 120; i++) {
      CoilState *s = &coils[i];
      n += snprintf(js + n, sizeof(js) - (size_t)n,
                    "%s{\"id\":%d,\"en\":%u,\"mag\":%u,\"dir\":%d,\"duty\":%u,\"i\":%.3f,\"lim\":%.3f}",
                    i ? "," : "",
                    i + 1, s->enabled, s->magnitude,
                    s->dir == DIR_FWD ? 1 : -1,
                    s->duty_applied, amps[i], s->i_limit_a);
    }
    if (n > 0 && n < (int)sizeof(js) - 360) {
      /* #region agent log */
      n += snprintf(js + n, sizeof(js) - (size_t)n,
                    "],\"mlx\":{\"ok\":%u,\"addr\":%u,\"sat\":%u,"
                    "\"bx\":%.3f,\"by\":%.3f,\"bz\":%.3f,\"babs\":%.3f},"
                    "\"dbg\":{\"ore1\":%lu,\"ore3\":%lu,\"empty\":%lu,\"lines\":%lu,"
                    "\"pca\":%u,\"dirty\":%u,\"isr\":%lu,\"over\":%u,"
                    "\"pr\":[%u,%u,%u,%u],\"pl\":[%u,%u,%u,%u]}}\r\n",
                    (unsigned)g_mlx.present, (unsigned)g_mlx.addr, (unsigned)g_mlx_sat,
                    (double)g_mlx_lab_mt[0], (double)g_mlx_lab_mt[1],
                    (double)g_mlx_lab_mt[2], (double)g_mlx_babs_mt,
                    (unsigned long)ore1, (unsigned long)ore3,
                    (unsigned long)empty, (unsigned long)lines,
                    (unsigned)pca, (unsigned)dirty, (unsigned long)isr, (unsigned)over,
                    (unsigned)pr[0], (unsigned)pr[1], (unsigned)pr[2], (unsigned)pr[3],
                    (unsigned)pl[0], (unsigned)pl[1], (unsigned)pl[2], (unsigned)pl[3]);
      /* #endregion */
      uart_write(js);
    } else {
      uart_write("{\"ok\":0,\"err\":\"overflow\"}\r\n");
    }
    return;
  }
  if (!strcmp(cmd, "status")) {
    for (uint8_t i = 0; i < NUM_COILS; i++) {
      Sense_ReadAmps(i);
      if (g_rotate_active) PCA9685_Flush();
    }
    CLI_PrintStatus();
    return;
  }
  if (!strcmp(cmd, "sense")) {
    for (uint8_t i = 0; i < NUM_COILS; i++) {
      float a = Sense_ReadAmps(i);
      if (g_rotate_active) PCA9685_Flush();
      uart_printf("M%d I=%.3f A\r\n", i + 1, a);
    }
    return;
  }
  if (!strcmp(cmd, "mode")) {
    if (argc < 2) { uart_write("usage: mode open|closed\r\n"); return; }
    if (str_ieq(argv[1], "open")) {
      g_ctrl_mode = CTRL_OPEN;
      uart_write("mode=open\r\n");
    } else if (str_ieq(argv[1], "closed")) {
      g_ctrl_mode = CTRL_CLOSED;
      uart_write("mode=closed (IS feedback)\r\n");
    } else {
      uart_write("usage: mode open|closed\r\n");
    }
    return;
  }
  if (!strcmp(cmd, "off")) {
    if (argc < 2) { uart_write("usage: off <M#|all>...\r\n"); return; }
    for (int a = 1; a < argc; a++) {
      if (str_ieq(argv[a], "all")) {
        Coil_AllOff();
        uart_write("all off\r\n");
        return;
      }
      uint8_t idx;
      if (parse_motor_token(argv[a], &idx) == 0) {
        Coil_SetEnabled(idx, 0);
        uart_printf("M%d off\r\n", idx + 1);
      } else {
        uart_printf("unknown motor '%s'\r\n", argv[a]);
      }
    }
    return;
  }
  if (!strcmp(cmd, "on")) {
    if (argc < 2) { uart_write("usage: on <M#>...\r\n"); return; }
    for (int a = 1; a < argc; a++) {
      uint8_t idx;
      if (parse_motor_token(argv[a], &idx) == 0) {
        if (g_ctrl_mode == CTRL_OPEN) coils[idx].duty_applied = coils[idx].magnitude;
        Coil_SetEnabled(idx, 1);
        uart_printf("M%d on mag=%u dir=%s\r\n", idx + 1, coils[idx].magnitude,
                    coils[idx].dir == DIR_FWD ? "+" : "-");
      } else {
        uart_printf("unknown motor '%s'\r\n", argv[a]);
      }
    }
    return;
  }
  if (!strcmp(cmd, "set")) {
    if (argc < 3) { uart_write("usage: set <M#|all> <0..255>\r\n"); return; }
    int mag = atoi(argv[2]);
    if (mag < 0) mag = 0;
    if (mag > 255) mag = 255;
    if (str_ieq(argv[1], "all")) {
      for (uint8_t i = 0; i < NUM_COILS; i++) Coil_SetMagnitude(i, (uint8_t)mag);
      uart_printf("all magnitude=%d\r\n", mag);
    } else {
      uint8_t idx;
      if (parse_motor_token(argv[1], &idx) == 0) {
        Coil_SetMagnitude(idx, (uint8_t)mag);
        uart_printf("M%d magnitude=%d\r\n", idx + 1, mag);
      } else {
        uart_printf("unknown motor '%s'\r\n", argv[1]);
      }
    }
    return;
  }
  if (!strcmp(cmd, "dir")) {
    if (argc < 3) { uart_write("usage: dir <M#|all> <+|->\r\n"); return; }
    CoilDir d = (argv[2][0] == '-' || argv[2][0] == 'r' || argv[2][0] == 'R')
                    ? DIR_REV : DIR_FWD;
    if (str_ieq(argv[1], "all")) {
      for (uint8_t i = 0; i < NUM_COILS; i++) Coil_SetDir(i, d);
      uart_printf("all dir=%s\r\n", d == DIR_FWD ? "+" : "-");
    } else {
      uint8_t idx;
      if (parse_motor_token(argv[1], &idx) == 0) {
        Coil_SetDir(idx, d);
        uart_printf("M%d dir=%s\r\n", idx + 1, d == DIR_FWD ? "+" : "-");
      } else {
        uart_printf("unknown motor '%s'\r\n", argv[1]);
      }
    }
    return;
  }
  if (!strcmp(cmd, "drive")) {
    if (argc < 4) { uart_write("usage: drive <M#> <0..255> <+|->\r\n"); return; }
    uint8_t idx;
    if (parse_motor_token(argv[1], &idx) != 0) {
      uart_printf("unknown motor '%s'\r\n", argv[1]);
      return;
    }
    int mag = atoi(argv[2]);
    if (mag < 0) mag = 0;
    if (mag > 255) mag = 255;
    CoilDir d = (argv[3][0] == '-') ? DIR_REV : DIR_FWD;
    Coil_SetMagnitude(idx, (uint8_t)mag);
    Coil_SetDir(idx, d);
    Coil_SetEnabled(idx, 1);
    uart_printf("M%d drive mag=%d dir=%s\r\n", idx + 1, mag, d == DIR_FWD ? "+" : "-");
    return;
  }
  if (!strcmp(cmd, "limit")) {
    if (argc < 3) { uart_write("usage: limit <M#|all> <0..255>\r\n"); return; }
    int lim = atoi(argv[2]);
    if (lim < 0) lim = 0;
    if (lim > 255) lim = 255;
    float amps = (lim == 0) ? 0.0f : ((float)lim / 255.0f) * I_FULLSCALE_A;
    if (str_ieq(argv[1], "all")) {
      for (uint8_t i = 0; i < NUM_COILS; i++) coils[i].i_limit_a = amps;
      uart_printf("all limit=%.3f A\r\n", amps);
    } else {
      uint8_t idx;
      if (parse_motor_token(argv[1], &idx) == 0) {
        coils[idx].i_limit_a = amps;
        uart_printf("M%d limit=%.3f A\r\n", idx + 1, amps);
      } else {
        uart_printf("unknown motor '%s'\r\n", argv[1]);
      }
    }
    return;
  }
  if (!strcmp(cmd, "contour")) {
    if (argc < 2) { uart_write("usage: contour on|off\r\n"); return; }
    Rotate_Stop();
    if (str_ieq(argv[1], "on")) {
      g_contour_active = 1;
      int n = (NUM_COILS < 4) ? NUM_COILS : 4;
      for (int i = 0; i < n; i++) {
        if (g_ctrl_mode == CTRL_OPEN) coils[i].duty_applied = coils[i].magnitude;
        Coil_SetEnabled((uint8_t)i, 1);
      }
      uart_write("contour ON (M1..M4)\r\n");
    } else if (str_ieq(argv[1], "off")) {
      g_contour_active = 0;
      int n = (NUM_COILS < 4) ? NUM_COILS : 4;
      for (int i = 0; i < n; i++) Coil_SetEnabled((uint8_t)i, 0);
      uart_write("contour OFF\r\n");
    } else {
      uart_write("usage: contour on|off\r\n");
    }
    return;
  }
  if (!strcmp(cmd, "program")) {
    if (argc < 2) { uart_write("usage: program P1|P2|stop\r\n"); return; }
    if (str_ieq(argv[1], "stop") || str_ieq(argv[1], "off")) {
      Coil_AllOff();
      uart_write("program stop\r\n");
      return;
    }
    Rotate_Stop();
    if (str_ieq(argv[1], "P1") || str_ieq(argv[1], "p1")) {
      int n = (NUM_COILS < 4) ? NUM_COILS : 4;
      g_contour_active = 1;
      for (int i = 0; i < n; i++) {
        if (g_ctrl_mode == CTRL_OPEN) coils[i].duty_applied = coils[i].magnitude;
        Coil_SetEnabled((uint8_t)i, 1);
      }
      uart_write("program P1: M1..M4 ON\r\n");
      return;
    }
    if (str_ieq(argv[1], "P2") || str_ieq(argv[1], "p2")) {
      int n = (NUM_COILS < 4) ? NUM_COILS : 4;
      g_contour_active = 1;
      for (int i = 0; i < n; i++) {
        Coil_SetMagnitude((uint8_t)i, 128);
        Coil_SetDir((uint8_t)i, DIR_FWD);
        Coil_SetEnabled((uint8_t)i, 1);
      }
      uart_write("program P2: M1..M4 @128 FWD\r\n");
      return;
    }
    uart_write("usage: program P1|P2|stop\r\n");
    return;
  }
  if (!strcmp(cmd, "rotate")) {
    if (argc < 2) { uart_write("usage: rotate on|off [Hz] [0..255]\r\n"); return; }
    if (str_ieq(argv[1], "on") || str_ieq(argv[1], "y")) {
      Field_ApplyOptionalFreqAmp(argc, argv, 2);
      Field_Start(FIELD_ROTATE_Y);
      uart_printf("rotate ON axis=y freq=%.2f amp=%u dir=%s (ramping)\r\n",
                  (double)g_rotate_freq_hz, (unsigned)g_rotate_amp,
                  rotate_dir_str(g_rotate_dir_want));
      /* #region agent log */
      dbg_print_snapshot("rotate_on");
      /* #endregion */
    } else if (str_ieq(argv[1], "off")) {
      Rotate_RequestStop();
      uart_write("rotate OFF (ramping)\r\n");
      /* #region agent log */
      dbg_print_snapshot("rotate_off");
      /* #endregion */
    } else {
      uart_write("usage: rotate on|off [Hz] [0..255]\r\n");
    }
    return;
  }
  if (!strcmp(cmd, "revolve")) {
    if (argc < 2) { uart_write("usage: revolve x|z|tilt [Hz] [0..255]  |  revolve off\r\n"); return; }
    if (str_ieq(argv[1], "off")) {
      Rotate_RequestStop();
      uart_write("revolve OFF (ramping)\r\n");
      /* #region agent log */
      dbg_print_snapshot("revolve_off");
      /* #endregion */
      return;
    }
    FieldProgram rp;
    if (str_ieq(argv[1], "x") || str_ieq(argv[1], "yz")) {
      rp = FIELD_REVOLVE_X;
    } else if (str_ieq(argv[1], "z") || str_ieq(argv[1], "xy")) {
      rp = FIELD_REVOLVE_Z;
    } else if (str_ieq(argv[1], "tilt") || str_ieq(argv[1], "tilted") ||
               str_ieq(argv[1], "n")) {
      rp = FIELD_REVOLVE_TILT;
    } else {
      uart_write("usage: revolve x|z|tilt [Hz] [0..255]  |  revolve off\r\n");
      return;
    }
    Field_ApplyOptionalFreqAmp(argc, argv, 2);
    Revolve_ClearPark();
    Field_Start(rp);
    if (rp == FIELD_REVOLVE_TILT) {
      float sc = Tilt_PreviewScale(g_rotate_amp);
      uart_printf("revolve ON axis=tilt gamma=%.2f freq=%.2f amp=%u dir=%s scale=%.4f%s (ramping)\r\n",
                  (double)g_tilt_gamma_want,
                  (double)g_rotate_freq_hz, (unsigned)g_rotate_amp,
                  rotate_dir_str(g_rotate_dir_want),
                  (double)sc, sc < 1.0f ? " limited" : "");
    } else {
      uart_printf("revolve ON axis=%s freq=%.2f amp=%u dir=%s (ramping)\r\n",
                  field_axis_str(g_field_prog_want),
                  (double)g_rotate_freq_hz, (unsigned)g_rotate_amp,
                  rotate_dir_str(g_rotate_dir_want));
    }
    /* #region agent log */
    dbg_print_snapshot("revolve_on");
    /* #endregion */
    return;
  }
  if (!strcmp(cmd, "freq")) {
    if (argc < 2) { uart_write("usage: freq <Hz>\r\n"); return; }
    float f = (float)atof(argv[1]);
    if (f < ROTATE_FREQ_MIN_HZ) f = ROTATE_FREQ_MIN_HZ;
    if (f > ROTATE_FREQ_MAX_HZ) f = ROTATE_FREQ_MAX_HZ;
    g_rotate_freq_hz = f;
    Rotate_RecalcPhaseInc();
    uart_printf("freq=%.2f Hz\r\n", (double)g_rotate_freq_hz);
    return;
  }
  if (!strcmp(cmd, "amp")) {
    if (argc < 2) { uart_write("usage: amp <0..255>\r\n"); return; }
    int a = atoi(argv[1]);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    g_rotate_amp = (uint8_t)a;
    for (int i = 0; i < NUM_ROTATE_COILS && i < NUM_COILS; i++) {
      coils[i].magnitude = g_rotate_amp;
    }
    uart_printf("amp=%u\r\n", (unsigned)g_rotate_amp);
    /* #region agent log */
    dbg_print_snapshot("amp");
    /* #endregion */
    return;
  }
  if (!strcmp(cmd, "corner") || !strcmp(cmd, "q0") || !strcmp(cmd, "q1") ||
      !strcmp(cmd, "q2") || !strcmp(cmd, "q3")) {
    const char *tok = cmd;
    int q = -2;
    if (!strcmp(cmd, "corner")) {
      if (argc < 2) {
        uart_write("usage: corner Q0|Q1|Q2|Q3|run\r\n");
        return;
      }
      tok = argv[1];
    }
    if (str_ieq(tok, "run") || str_ieq(tok, "spin")) {
      q = -1;
    } else if ((tok[0] == 'q' || tok[0] == 'Q') && tok[1] >= '0' && tok[1] <= '3' &&
               tok[2] == '\0') {
      q = tok[1] - '0';
    } else if (tok[0] >= '0' && tok[0] <= '3' && tok[1] == '\0') {
      q = tok[0] - '0';
    } else {
      uart_write("usage: corner Q0|Q1|Q2|Q3|run\r\n");
      return;
    }
    if (q < 0) {
      Revolve_ClearPark();
      if (g_rotate_active && field_is_revolve(g_field_prog)) {
        g_revolve_phase_hold = 0;
      }
      uart_write("corner=run\r\n");
    } else {
      Revolve_GotoCorner((uint8_t)q);
      uart_printf("corner=Q%u (%s)\r\n", (unsigned)q,
                  q == 0 ? "+Bh" : q == 1 ? "+Bv" : q == 2 ? "-Bh" : "-Bv");
    }
    return;
  }
  if (!strcmp(cmd, "rev_hv") || !strcmp(cmd, "revhv") || !strcmp(cmd, "ellipse")) {
    if (argc < 3) {
      uart_write("usage: rev_hv <h%> <v%>   (1..200; Ah=amp*h/100, Av=amp*v/100; 100 100=circle)\r\n");
      return;
    }
    g_revolve_h_pct = clamp_u8_pct(atoi(argv[1]));
    g_revolve_v_pct = clamp_u8_pct(atoi(argv[2]));
    {
      float sc = Tilt_PreviewScale(g_rotate_amp);
      uart_printf("rev_hv h=%u%% v=%u%%%s\r\n",
                  (unsigned)g_revolve_h_pct, (unsigned)g_revolve_v_pct,
                  (sc < 1.0f && field_is_tilt(g_field_prog_want))
                    ? "  (tilt shared-scale will limit)" : "");
    }
    return;
  }
  if (!strcmp(cmd, "gamma") || !strcmp(cmd, "tilt") || !strcmp(cmd, "tilt_gamma")) {
    float g;
    if (argc < 2) {
      uart_printf("gamma=%.2f deg (live=%.2f)  range 0..90 from +X toward +Z\r\n",
                  (double)g_tilt_gamma_want, (double)g_tilt_gamma_live);
      return;
    }
    if (!field_tilt_parse_gamma(argv[1], &g)) {
      uart_write("usage: gamma <0..90>   (degrees from +X toward +Z; no wrap)\r\n");
      return;
    }
    Tilt_SetGamma(g);
    {
      float sc = Tilt_PreviewScale(g_rotate_amp);
      uart_printf("gamma=%.2f deg%s%s\r\n",
                  (double)g_tilt_gamma_want,
                  (g_rotate_active && field_is_tilt(g_field_prog) &&
                   g_tilt_gamma_live != g_tilt_gamma_want)
                    ? " (ramping through 0)" : "",
                  sc < 1.0f ? "  tilt_scale limited" : "");
    }
    return;
  }
  if (!strcmp(cmd, "mlx")) {
    if (argc < 2) {
      Mlx_PrintStatus();
      return;
    }
    if (str_ieq(argv[1], "on") || str_ieq(argv[1], "1")) {
      g_mlx_en = 1;
      if (!g_mlx.present) mlx_try_init();
      uart_printf("mlx=on present=%u\r\n", (unsigned)g_mlx.present);
      return;
    }
    if (str_ieq(argv[1], "off") || str_ieq(argv[1], "0")) {
      g_mlx_en = 0;
      uart_write("mlx=off\r\n");
      return;
    }
    if (str_ieq(argv[1], "scan")) {
      mlx_try_init();
      Mlx_PrintStatus();
      return;
    }
    if (str_ieq(argv[1], "map")) {
      uint8_t src[3];
      int8_t sgn[3];
      uint8_t used = 0;
      int i;
      if (argc < 5) {
        uart_write("usage: mlx map <labX> <labY> <labZ>   e.g. mlx map x y z\r\n");
        return;
      }
      for (i = 0; i < 3; i++) {
        if (!mlx_parse_axis_tok(argv[2 + i], &src[i], &sgn[i])) {
          uart_write("mlx map: need x|y|z with optional + or - prefix\r\n");
          return;
        }
        if (used & (uint8_t)(1u << src[i])) {
          uart_write("mlx map: each chip axis once\r\n");
          return;
        }
        used |= (uint8_t)(1u << src[i]);
      }
      for (i = 0; i < 3; i++) {
        g_mlx_src[i] = src[i];
        g_mlx_sgn[i] = sgn[i];
      }
      uart_printf("mlx map labXYZ=%c%c %c%c %c%c\r\n",
                  sgn[0] < 0 ? '-' : '+', "xyz"[src[0]],
                  sgn[1] < 0 ? '-' : '+', "xyz"[src[1]],
                  sgn[2] < 0 ? '-' : '+', "xyz"[src[2]]);
      return;
    }
    if (str_ieq(argv[1], "offset")) {
      if (!g_mlx.present) { uart_write("mlx missing\r\n"); return; }
      if (g_rotate_active) Rotate_Stop();
      if (mlx_take_offset()) {
        uart_printf("mlx B0=%.3f %.3f %.3f mT\r\n",
                    (double)g_mlx_b0_mt[0], (double)g_mlx_b0_mt[1],
                    (double)g_mlx_b0_mt[2]);
      } else {
        uart_write("mlx offset failed\r\n");
      }
      return;
    }
    uart_write("usage: mlx | on|off | scan | map | offset\r\n");
    return;
  }
  if (!strcmp(cmd, "teleplot") || !strcmp(cmd, "tp")) {
    if (argc < 2) { uart_write("usage: teleplot on|off\r\n"); return; }
    if (str_ieq(argv[1], "on") || str_ieq(argv[1], "1")) {
      g_teleplot_en = 1;
      uart_write("teleplot=on (USART1 ~50 Hz; open Teleplot on Serial Monitor)\r\n");
    } else if (str_ieq(argv[1], "off") || str_ieq(argv[1], "0")) {
      g_teleplot_en = 0;
      uart_write("teleplot=off\r\n");
    } else {
      uart_write("usage: teleplot on|off\r\n");
    }
    return;
  }
  if (!strcmp(cmd, "rot_dir") || !strcmp(cmd, "rotdir")) {
    if (argc < 2) { uart_write("usage: rot_dir cw|ccw\r\n"); return; }
    if (str_ieq(argv[1], "cw") || str_ieq(argv[1], "+") || str_ieq(argv[1], "fwd")) {
      Rotate_SetDir(ROT_CW);
      uart_write("rot_dir=cw\r\n");
    } else if (str_ieq(argv[1], "ccw") || str_ieq(argv[1], "-") || str_ieq(argv[1], "rev")) {
      Rotate_SetDir(ROT_CCW);
      uart_write("rot_dir=ccw\r\n");
    } else {
      uart_write("usage: rot_dir cw|ccw\r\n");
    }
    return;
  }
  if (!strcmp(cmd, "axis") || !strcmp(cmd, "rot_axis")) {
    if (argc < 2) { uart_write("usage: axis y|x|z|tilt\r\n"); return; }
    if (str_ieq(argv[1], "y") || str_ieq(argv[1], "xz") || str_ieq(argv[1], "rotate")
        || str_ieq(argv[1], "inplane")) {
      Field_SetProgram(FIELD_ROTATE_Y);
      uart_write("axis=y program=rotate (XZ, all four coils)\r\n");
    } else if (str_ieq(argv[1], "x") || str_ieq(argv[1], "yz")) {
      Field_SetProgram(FIELD_REVOLVE_X);
      uart_write("axis=x program=revolve (YZ, M1=I1 M3=I2)\r\n");
    } else if (str_ieq(argv[1], "z") || str_ieq(argv[1], "xy")) {
      Field_SetProgram(FIELD_REVOLVE_Z);
      uart_write("axis=z program=revolve (XY, M2=I1 M4=I2)\r\n");
    } else if (str_ieq(argv[1], "tilt") || str_ieq(argv[1], "tilted") ||
               str_ieq(argv[1], "n")) {
      Field_SetProgram(FIELD_REVOLVE_TILT);
      uart_printf("axis=tilt program=revolve (n in XZ, gamma=%.2f deg, all four)\r\n",
                  (double)g_tilt_gamma_want);
    } else {
      uart_write("usage: axis y|x|z|tilt\r\n");
    }
    return;
  }

  uart_write("unknown command — type help\r\n");
}

static void CLI_PollPort(CliPort *port)
{
  uint8_t ch;
  while (uart_getc_usart(port->usart, port->hal, &ch)) {
    cli_reply = port;
    if (ch == '\r' || ch == '\n') {
      if (port->len > 0) {
        port->buf[port->len] = '\0';
        if (port->echo) uart_write("\r\n");
        /* #region agent log */
        g_dbg_cli_lines++;
        /* #endregion */
        CLI_HandleLine(port->buf);
        if (port->echo) uart_write("> ");
        port->len = 0;
      } else {
        /* #region agent log */
        g_dbg_empty_cr++;
        /* #endregion */
        if (port->echo) uart_write("\r\n> ");
      }
    } else if (ch == 0x08 || ch == 0x7F) {
      if (port->len > 0) {
        port->len--;
        if (port->echo) uart_write("\b \b");
      }
    } else if (port->len < CLI_LINE_MAX - 1 && ch >= 32 && ch < 127) {
      port->buf[port->len++] = (char)ch;
      if (port->echo) uart_putc(ch);
    }
  }
}

static void CLI_Poll(void)
{
  CLI_PollPort(&cli_ports[0]);
  CLI_PollPort(&cli_ports[1]);
}

/* -------------------- main -------------------- */
int main(void)
{
  CPU_CACHE_Enable();
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_I2C1_Init();
  MX_I2C2_Init();
  MX_TIM6_Init();
  MX_ADC1_Init();
  PCA9685_Init();

  memset(coils, 0, sizeof(coils));
  for (int i = 0; i < NUM_COILS; i++) {
    coils[i].i_limit_a = I_FULLSCALE_A; /* default soft cap ≈ 2 A */
  }
  Rotate_RecalcPhaseInc();
  g_rotate_amp_live = 0;

  /* Port 0: Serial Monitor. Port 1: ESP32 bridge (no echo). */
  cli_ports[0].usart = USART1;
  cli_ports[0].hal = &huart1;
  cli_ports[0].len = 0;
  cli_ports[0].echo = 1;
  cli_ports[1].usart = USART3;
  cli_ports[1].hal = &huart3;
  cli_ports[1].len = 0;
  cli_ports[1].echo = 0;
  cli_reply = &cli_ports[0];

  Coil_AllOff();
  Sense_CalibrateOffsets();
  /* #region agent log */
  dbg_print_snapshot("isoff");
  /* #endregion */

  /* Onboard ST7735 status UI (SPI4 on PE10–PE14) */
  ST7735_Init();
  DisplayUI_Init();
  mlx_try_init();
  Display_PushStatus();

  if (g_pca_ok) {
    uart_write_to(&cli_ports[0],
                  g_mlx.present
                    ? "\r\n\r\nH743 coil driver ready (PCA9685 PWM, MLX90393). Type 'help'.\r\n> "
                    : "\r\n\r\nH743 coil driver ready (PCA9685 PWM, no MLX on I2C2). Type 'help'.\r\n> ");
  } else {
    uart_write_to(&cli_ports[0],
                  "\r\n\r\nH743 ready but PCA9685 MISSING on I2C1 0x40 — PWM dead. Type 'help'.\r\n> ");
  }
  uart_write_to(&cli_ports[1], "BRIDGE_OK\r\n");
  last_loop_ms = HAL_GetTick();

  uint32_t led_ms = HAL_GetTick();
  uint32_t ui_ms = HAL_GetTick();
  uint32_t tele_ms = HAL_GetTick();
  while (1) {
    CLI_Poll();
    PCA9685_Flush();
    if (g_mlx_en && g_mlx.present) {
      MlxSample s;
      uint8_t fresh = 0;
      MLX90393_Poll(&g_mlx, &s, &fresh);
      if (fresh) Mlx_ApplySample(&s);
    }

    uint32_t now = HAL_GetTick();
    if ((now - last_loop_ms) >= (1000U / CURRENT_LOOP_HZ)) {
      last_loop_ms = now;
      CurrentLoop_Update();
    }

    if (g_teleplot_en && (now - tele_ms) >= (1000U / TELEPLOT_HZ)) {
      tele_ms = now;
      Teleplot_Emit();
    }

    if ((now - ui_ms) >= 100U) { /* ~10 Hz TFT refresh */
      ui_ms = now;
      Display_PushStatus();
    }

    if ((now - led_ms) >= 500U) {
      led_ms = now;
      HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_3);
    }
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

/* Sine sample clock for M1–M4; PCA9685 I2C flush runs in the main loop. */
void USART1_IRQHandler(void)
{
  uart_rxq_push_isr(&rxq1);
}

void USART3_IRQHandler(void)
{
  uart_rxq_push_isr(&rxq3);
}

void TIM6_DAC_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim6);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance != TIM6 || !g_rotate_active) return;

  /* #region agent log */
  g_dbg_isr++;
  /* #endregion */
  FieldProgram prog = g_field_prog;
  uint8_t hold = g_revolve_phase_hold;
  uint8_t travel = g_revolve_travel;
  if (prog == FIELD_ROTATE_Y) {
    g_phase += g_phase_inc;
  } else if (travel) {
    uint32_t prev = g_phase;
    uint32_t tgt = revolve_corner_phase(g_revolve_corner, g_rotate_dir);
    uint32_t step, off;
    g_phase += g_phase_inc;
    step = g_phase - prev;
    off = tgt - prev;
    if (off > 0u && off <= step) {
      g_phase = tgt;
      g_revolve_travel = 0;
      g_revolve_phase_hold = 1;
    }
  } else if (!hold) {
    g_phase += g_phase_inc;
  }
  uint16_t si = lut_from_phase(g_phase); /* 1 LUT step = 1° */
  uint8_t amp = g_rotate_amp_live;

  if (prog == FIELD_ROTATE_Y) {
    /* M1=M3 ∝ cos ωt, M2=M4 ∝ sin ωt; same signed current within each pair. */
    int16_t d13 = sine_scaled(lut_add(si, (int16_t)SINE_LUT_90), amp); /* cos */
    int16_t d24 = sine_scaled(si, amp);                               /* sin */
    if (g_rotate_dir == ROT_CCW) d24 = (int16_t)(-d24);
    Coil_ApplyPairedSignedDuty(0, 2, d13);
    Coil_ApplyPairedSignedDuty(1, 3, d24);
  } else {
    uint8_t a_cmd = amp;
    uint8_t amp_h, amp_v;
    if (hold) a_cmd = revolve_envelope_amp(amp, g_revolve_env_peak);
    amp_h = revolve_scaled_amp(a_cmd, g_revolve_h_pct);
    amp_v = revolve_scaled_amp(a_cmd, g_revolve_v_pct);
    if (prog == FIELD_REVOLVE_TILT) {
      int16_t cmd[4];
      float sg = g_tilt_sin_g;
      float cg = g_tilt_cos_g;
      float scale;
      revolve_tilt_currents(g_phase, amp_h, amp_v, g_rotate_dir, sg, cg,
                            cmd, &scale);
      g_tilt_scale = scale;
      Coil_ApplySignedDuty(0, cmd[0]);
      Coil_ApplySignedDuty(1, cmd[1]);
      Coil_ApplySignedDuty(2, cmd[2]);
      Coil_ApplySignedDuty(3, cmd[3]);
    } else {
      int16_t i1, i2;
      revolve_pair_currents(g_phase, amp_h, amp_v, g_rotate_dir, &i1, &i2);
      if (prog == FIELD_REVOLVE_Z) {
        Coil_ApplySignedDuty(0, 0);
        Coil_ApplySignedDuty(2, 0);
        Coil_ApplySignedDuty(1, i1); /* M2 = I1 */
        Coil_ApplySignedDuty(3, i2); /* M4 = I2 */
      } else {
        Coil_ApplySignedDuty(1, 0);
        Coil_ApplySignedDuty(3, 0);
        Coil_ApplySignedDuty(0, i1); /* M1 = I1 */
        Coil_ApplySignedDuty(2, i2); /* M3 = I2 */
      }
    }
  }
}

void NMI_Handler(void) {}
void HardFault_Handler(void) { while (1) {} }
void MemManage_Handler(void) { while (1) {} }
void BusFault_Handler(void) { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
