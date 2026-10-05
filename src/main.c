/**
 * @file main.c
 * @brief STM32H743VIT6 (WeAct Mini) coil driver for up to 6× BTS7960 / IBT-2.
 *
 * Each BTS7960 drives one coil (≤ ~2 A). Magnitude is native 0–255 PWM duty
 * at a high carrier (~40 kHz). Direction selects RPWM vs LPWM (B-field polarity).
 *
 * Dual modes:
 *   DC     — constant magnitude/dir per coil (open- or closed-loop IS)
 *   Rotate — open-loop quadrature sine on M1–M4 (M1∥M3 @ 0°, M2∥M4 @ ±90°)
 *            shared freq (0.1–50 Hz) + amp; TIM6 ~10 kHz sine ISR
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
 * Wiring (default pin map — change COIL_HW[] if your harness differs):
 *
 *   Coil  RPWM     LPWM     EN      IS(ADC)
 *   M1    PD14     PD15     PD10    PA0   (TIM4 — frees PE for onboard TFT)
 *   M2    PE5      PE6      PE4     PA1   (TIM15)
 *   M3    PC6      PC7      PC5     PA2
 *   M4    PC8      PC9      PC4     PA3
 *   M5    PA6      PA7      PB10    PA4
 *   M6    PB0      PB1      PB11    PA5
 *
 * Onboard ST7735 (SPI4): PE12 SCK, PE14 MOSI, PE11 CS, PE13 DC, PE10 BL.
 *
 * BTS7960 / IBT-2: tie R_EN+L_EN to EN, VCC=5 V logic, VS = coil supply.
 * IS → MCU ADC through the module sense resistor (see IS_RIS_OHM).
 */

#include "stm32h7xx_hal.h"
#include "st7735.h"
#include "display_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

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
#define NUM_COILS 6
#endif
#if (NUM_COILS < 1) || (NUM_COILS > 6)
#error "NUM_COILS must be 1..6"
#endif

#ifndef PWM_TARGET_HZ
#define PWM_TARGET_HZ 40000U
#endif

#define PWM_ARR                 255U   /* duty command == CCR, 0..255 */
#define UART_BAUD               115200U
#define CLI_LINE_MAX            128
#define CURRENT_LOOP_HZ         200U   /* sense / closed-loop update rate */

/* Quadrature rotating field (M1∥M3 @ 0°, M2∥M4 @ ±90°) */
#define NUM_ROTATE_COILS        4
#define SINE_UPDATE_HZ          10000U
#define SINE_LUT_SIZE           256
#define ROTATE_FREQ_MIN_HZ      0.1f
#define ROTATE_FREQ_MAX_HZ      50.0f

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
  ROT_CW = 0,  /* Phase B = +90° (cos) */
  ROT_CCW = 1  /* Phase B = −90° (−cos) */
} RotateDir;

typedef struct {
  GPIO_TypeDef *port;
  uint16_t      pin;
  uint32_t      af;
} GpioAf;

typedef struct {
  TIM_TypeDef  *tim;
  uint32_t      ch_rpwm;
  uint32_t      ch_lpwm;
  GpioAf        rpwm;
  GpioAf        lpwm;
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

/* -------------------- pin / timer map -------------------- */
static const CoilHw COIL_HW[6] = {
  /* M1 */ { TIM4, TIM_CHANNEL_3, TIM_CHANNEL_4,
             { GPIOD, GPIO_PIN_14, GPIO_AF2_TIM4 },
             { GPIOD, GPIO_PIN_15, GPIO_AF2_TIM4 },
             GPIOD, GPIO_PIN_10, ADC_CHANNEL_16 }, /* IS on PA0 = ADC1_INP16 */
  /* M2 */ { TIM15, TIM_CHANNEL_1, TIM_CHANNEL_2,
             { GPIOE, GPIO_PIN_5,  GPIO_AF4_TIM15 },
             { GPIOE, GPIO_PIN_6,  GPIO_AF4_TIM15 },
             GPIOE, GPIO_PIN_4,  ADC_CHANNEL_17 }, /* IS on PA1 */
  /* M3 */ { TIM8, TIM_CHANNEL_1, TIM_CHANNEL_2,
             { GPIOC, GPIO_PIN_6,  GPIO_AF3_TIM8 },
             { GPIOC, GPIO_PIN_7,  GPIO_AF3_TIM8 },
             GPIOC, GPIO_PIN_5,  ADC_CHANNEL_14 }, /* IS on PA2 */
  /* M4 */ { TIM8, TIM_CHANNEL_3, TIM_CHANNEL_4,
             { GPIOC, GPIO_PIN_8,  GPIO_AF3_TIM8 },
             { GPIOC, GPIO_PIN_9,  GPIO_AF3_TIM8 },
             GPIOC, GPIO_PIN_4,  ADC_CHANNEL_15 }, /* IS on PA3 */
  /* M5 */ { TIM3, TIM_CHANNEL_1, TIM_CHANNEL_2,
             { GPIOA, GPIO_PIN_6,  GPIO_AF2_TIM3 },
             { GPIOA, GPIO_PIN_7,  GPIO_AF2_TIM3 },
             GPIOB, GPIO_PIN_10, ADC_CHANNEL_18 }, /* IS on PA4 */
  /* M6 */ { TIM3, TIM_CHANNEL_3, TIM_CHANNEL_4,
             { GPIOB, GPIO_PIN_0,  GPIO_AF2_TIM3 },
             { GPIOB, GPIO_PIN_1,  GPIO_AF2_TIM3 },
             GPIOB, GPIO_PIN_11, ADC_CHANNEL_19 }, /* IS on PA5 */
};

/* PA0..PA5 ADC1 channels on H743: INP16..INP19 for PA0..PA3, INP18/19 for PA4/PA5
 * Correct H743 ADC1 mapping:
 *   PA0 ADC1_INP16, PA1 ADC1_INP17, PA2 ADC1_INP14, PA3 ADC1_INP15,
 *   PA4 ADC1_INP18, PA5 ADC1_INP19
 */

static UART_HandleTypeDef huart1;
static UART_HandleTypeDef huart3;
static ADC_HandleTypeDef  hadc1;
static TIM_HandleTypeDef  htim3, htim4, htim8, htim15, htim6;

static CoilState coils[NUM_COILS];
static CtrlMode  g_ctrl_mode = CTRL_OPEN;
static uint8_t   g_contour_active;

/* Rotating-field state (open-loop sine on M1–M4) */
static volatile uint8_t  g_rotate_active;
static float             g_rotate_freq_hz = 1.0f;
static uint8_t           g_rotate_amp = 0;
static volatile uint8_t  g_rotate_amp_live = 0; /* may be soft-clamped */
static RotateDir         g_rotate_dir = ROT_CW;
static volatile uint32_t g_phase;
static volatile uint32_t g_phase_inc;

/* Q15 sine table: sin(2π i / 256) * 32767 */
static const int16_t SINE_LUT[SINE_LUT_SIZE] = {
       0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
    6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
   12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
   18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
   23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
   27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
   30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
   32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
   32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
   32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
   30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
   27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
   23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
   18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
   12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
    6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
       0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
   -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
  -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
  -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
  -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
  -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
  -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
  -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
  -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
  -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
  -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
  -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
  -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
  -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
  -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
   -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804
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

static uint32_t last_loop_ms;

/* -------------------- prototypes -------------------- */
void Error_Handler(void);
static void SystemClock_Config(void);
static void CPU_CACHE_Enable(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIMx_Init(void);
static void MX_TIM6_Init(void);
static TIM_HandleTypeDef *tim_handle_for(const CoilHw *hw);
static void Display_PushStatus(void);
static void Coil_ApplyOutputs(uint8_t idx);
static void Coil_ApplySignedDuty(uint8_t idx, int16_t signed_duty);
static void Coil_AllOff(void);
static void Rotate_RecalcPhaseInc(void);
static void Rotate_Stop(void);
static void Rotate_Start(void);
static void Rotate_StopIfDcConflict(uint8_t idx);
static float Sense_ReadAmps(uint8_t idx);
static void CurrentLoop_Update(void);
static void CLI_PollPort(CliPort *port);
static void CLI_HandleLine(char *line);
static void uart_write(const char *s);
static void uart_printf(const char *fmt, ...);

/* -------------------- UART helpers (register-level; HAL timeout-safe) ---- */
static void uart_clear_errors_usart(USART_TypeDef *u, UART_HandleTypeDef *h)
{
  if (u->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
    u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    (void)u->RDR;
  }
  if (h) {
    if (h->gState != HAL_UART_STATE_READY) h->gState = HAL_UART_STATE_READY;
    if (h->RxState != HAL_UART_STATE_READY) h->RxState = HAL_UART_STATE_READY;
  }
}

static int uart_getc_usart(USART_TypeDef *u, UART_HandleTypeDef *h, uint8_t *ch)
{
  uart_clear_errors_usart(u, h);
  if ((u->ISR & USART_ISR_RXNE_RXFNE) == 0U) {
    return 0;
  }
  *ch = (uint8_t)(u->RDR & 0xFFU);
  return 1;
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
  while (*s) {
    uart_putc((uint8_t)*s++);
  }
}

static void uart_printf(const char *fmt, ...)
{
  char tmp[256];
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
}

static uint32_t timer_clock_hz(TIM_TypeDef *tim)
{
  /* With APB1/APB2 divider = 2, timer clocks are 2× PCLK (= HCLK = 240 MHz). */
  (void)tim;
  return HAL_RCC_GetHCLKFreq();
}

static void tim_pwm_gpio(const GpioAf *p)
{
  GPIO_InitTypeDef g = {0};
  gpio_clk_enable(p->port);
  g.Pin = p->pin;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = p->af;
  HAL_GPIO_Init(p->port, &g);
}

static void tim_apply_base(TIM_HandleTypeDef *htim, TIM_TypeDef *inst)
{
  uint32_t tclk = timer_clock_hz(inst);
  uint32_t target = (uint32_t)PWM_TARGET_HZ * (PWM_ARR + 1U);
  uint32_t psc = (tclk + target / 2U) / target;
  if (psc < 1U) psc = 1U;

  htim->Instance = inst;
  htim->Init.Prescaler = psc - 1U;
  htim->Init.CounterMode = TIM_COUNTERMODE_UP;
  htim->Init.Period = PWM_ARR;
  htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim->Init.RepetitionCounter = 0;
  htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(htim) != HAL_OK) {
    Error_Handler();
  }

  TIM_MasterConfigTypeDef mc = {0};
  mc.MasterOutputTrigger = TIM_TRGO_RESET;
  mc.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  mc.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  HAL_TIMEx_MasterConfigSynchronization(htim, &mc);
}

static void tim_start_channel(TIM_HandleTypeDef *htim, uint32_t ch)
{
  TIM_OC_InitTypeDef oc = {0};
  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = 0;
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  oc.OCIdleState = TIM_OCIDLESTATE_RESET;
  oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(htim, &oc, ch) != HAL_OK) {
    Error_Handler();
  }
  HAL_TIM_PWM_Start(htim, ch);
  /* Advanced timers need MOE for outputs */
  if (htim->Instance == TIM1 || htim->Instance == TIM8) {
    __HAL_TIM_MOE_ENABLE(htim);
  }
}

static void MX_TIMx_Init(void)
{
  __HAL_RCC_TIM3_CLK_ENABLE();
  __HAL_RCC_TIM4_CLK_ENABLE();
  __HAL_RCC_TIM8_CLK_ENABLE();
  __HAL_RCC_TIM15_CLK_ENABLE();

  for (int i = 0; i < NUM_COILS; i++) {
    tim_pwm_gpio(&COIL_HW[i].rpwm);
    tim_pwm_gpio(&COIL_HW[i].lpwm);
  }

  tim_apply_base(&htim3, TIM3);
  tim_apply_base(&htim4, TIM4);
  tim_apply_base(&htim8, TIM8);
  tim_apply_base(&htim15, TIM15);

  /* Break/dead-time defaults for advanced timers (TIM8 only now) */
  TIM_BreakDeadTimeConfigTypeDef bd = {0};
  bd.OffStateRunMode = TIM_OSSR_DISABLE;
  bd.OffStateIDLEMode = TIM_OSSI_DISABLE;
  bd.LockLevel = TIM_LOCKLEVEL_OFF;
  bd.DeadTime = 0;
  bd.BreakState = TIM_BREAK_DISABLE;
  bd.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  bd.BreakFilter = 0;
  bd.Break2State = TIM_BREAK2_DISABLE;
  bd.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  bd.Break2Filter = 0;
  bd.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  HAL_TIMEx_ConfigBreakDeadTime(&htim8, &bd);

  for (int i = 0; i < NUM_COILS; i++) {
    TIM_HandleTypeDef *h = tim_handle_for(&COIL_HW[i]);
    tim_start_channel(h, COIL_HW[i].ch_rpwm);
    tim_start_channel(h, COIL_HW[i].ch_lpwm);
  }
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

  /* PA0..PA5 analog for IS */
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
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

static TIM_HandleTypeDef *tim_handle_for(const CoilHw *hw)
{
  if (hw->tim == TIM4) return &htim4;
  if (hw->tim == TIM8) return &htim8;
  if (hw->tim == TIM15) return &htim15;
  return &htim3;
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
  DisplayUI_Update(g_ctrl_mode == CTRL_OPEN ? "open" : "closed",
                   g_contour_active,
                   g_rotate_active,
                   g_rotate_freq_hz,
                   g_rotate_amp_live,
                   snap, NUM_COILS);
}

static void tim_set_ccr(TIM_HandleTypeDef *h, uint32_t ch, uint32_t pulse)
{
  switch (ch) {
    case TIM_CHANNEL_1: h->Instance->CCR1 = pulse; break;
    case TIM_CHANNEL_2: h->Instance->CCR2 = pulse; break;
    case TIM_CHANNEL_3: h->Instance->CCR3 = pulse; break;
    case TIM_CHANNEL_4: h->Instance->CCR4 = pulse; break;
    default: break;
  }
}

/* -------------------- coil control -------------------- */
static void Coil_ApplyOutputs(uint8_t idx)
{
  if (idx >= NUM_COILS) return;
  /* Rotate ISR owns M1–M4 PWM while active */
  if (g_rotate_active && idx < NUM_ROTATE_COILS) return;

  const CoilHw *hw = &COIL_HW[idx];
  CoilState *st = &coils[idx];
  TIM_HandleTypeDef *h = tim_handle_for(hw);

  uint16_t duty = 0;
  if (st->enabled && st->magnitude > 0) {
    duty = st->duty_applied ? st->duty_applied : st->magnitude;
  }

  if (!st->enabled || duty == 0) {
    tim_set_ccr(h, hw->ch_rpwm, 0);
    tim_set_ccr(h, hw->ch_lpwm, 0);
    HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_RESET);
    return;
  }

  HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_SET);
  if (st->dir == DIR_FWD) {
    tim_set_ccr(h, hw->ch_rpwm, duty);
    tim_set_ccr(h, hw->ch_lpwm, 0);
  } else {
    tim_set_ccr(h, hw->ch_rpwm, 0);
    tim_set_ccr(h, hw->ch_lpwm, duty);
  }
}

/* Bipolar H-bridge duty from signed sample (−255..+255). ISR-safe. */
static void Coil_ApplySignedDuty(uint8_t idx, int16_t signed_duty)
{
  if (idx >= NUM_COILS) return;
  const CoilHw *hw = &COIL_HW[idx];
  TIM_HandleTypeDef *h = tim_handle_for(hw);
  uint16_t mag;

  if (signed_duty > (int16_t)PWM_ARR) signed_duty = (int16_t)PWM_ARR;
  if (signed_duty < -(int16_t)PWM_ARR) signed_duty = -(int16_t)PWM_ARR;

  if (signed_duty >= 0) {
    mag = (uint16_t)signed_duty;
    if (mag == 0) {
      tim_set_ccr(h, hw->ch_rpwm, 0);
      tim_set_ccr(h, hw->ch_lpwm, 0);
      HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_RESET);
    } else {
      HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_SET);
      tim_set_ccr(h, hw->ch_rpwm, mag);
      tim_set_ccr(h, hw->ch_lpwm, 0);
    }
  } else {
    mag = (uint16_t)(-signed_duty);
    HAL_GPIO_WritePin(hw->en_port, hw->en_pin, GPIO_PIN_SET);
    tim_set_ccr(h, hw->ch_rpwm, 0);
    tim_set_ccr(h, hw->ch_lpwm, mag);
  }

  coils[idx].duty_applied = mag;
  coils[idx].dir = (signed_duty >= 0) ? DIR_FWD : DIR_REV;
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

static void Rotate_Stop(void)
{
  g_rotate_active = 0;
  g_contour_active = 0; /* idle header/json must not keep a stale contour flag */
  HAL_TIM_Base_Stop_IT(&htim6);
  g_rotate_amp_live = g_rotate_amp;

  int n = (NUM_COILS < NUM_ROTATE_COILS) ? NUM_COILS : NUM_ROTATE_COILS;
  for (int i = 0; i < n; i++) {
    coils[i].enabled = 0;
    coils[i].duty_applied = 0;
    coils[i].pi_integral = 0;
    Coil_ApplyOutputs((uint8_t)i);
  }
}

static void Rotate_Start(void)
{
#if (NUM_COILS < NUM_ROTATE_COILS)
  uart_write("rotate needs NUM_COILS>=4\r\n");
  return;
#else
  g_contour_active = 0;
  Rotate_RecalcPhaseInc();
  g_phase = 0;
  g_rotate_amp_live = g_rotate_amp;

  for (int i = 0; i < NUM_ROTATE_COILS; i++) {
    coils[i].enabled = 1;
    coils[i].pi_integral = 0;
    coils[i].magnitude = g_rotate_amp;
  }

  g_rotate_active = 1;
  HAL_TIM_Base_Start_IT(&htim6);
#endif
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
static float Sense_ReadAmps(uint8_t idx)
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
  float i = vis * (IS_K_RATIO / IS_RIS_OHM);
  coils[idx].i_meas_a = i;
  return i;
}

static void CurrentLoop_Update(void)
{
  for (uint8_t i = 0; i < NUM_COILS; i++) {
    CoilState *st = &coils[i];

    /* Rotating field owns M1–M4; sense for soft amp clamp / display only */
    if (g_rotate_active && i < NUM_ROTATE_COILS) {
      float i_meas = Sense_ReadAmps(i);
      if (st->i_limit_a > 0.0f && i_meas > st->i_limit_a) {
        if (g_rotate_amp_live > 0) g_rotate_amp_live--;
      }
      continue;
    }

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

  /* Recover soft-clamped rotate amp toward commanded amp when under limit */
  if (g_rotate_active && g_rotate_amp_live < g_rotate_amp) {
    uint8_t under = 1;
    for (uint8_t i = 0; i < NUM_ROTATE_COILS && i < NUM_COILS; i++) {
      if (coils[i].i_limit_a > 0.0f &&
          coils[i].i_meas_a > coils[i].i_limit_a * 0.9f) {
        under = 0;
        break;
      }
    }
    if (under) g_rotate_amp_live++;
  }
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

static void CLI_PrintHelp(void)
{
  uart_write(
    "\r\n=== H743 BTS7960 coil driver ===\r\n"
    "help                         show this help\r\n"
    "status                       show coil states + currents\r\n"
    "sense                        sample IS currents once\r\n"
    "on <M1..M6> [..]             enable coil(s) [DC]\r\n"
    "off <M1..M6|all> [..]        disable coil(s)\r\n"
    "set <M#|all> <0..255>        magnitude (duty / Iref scale) [DC]\r\n"
    "dir <M#|all> <+|->           B-field / current direction [DC]\r\n"
    "drive <M#> <0..255> <+|->    set mag+dir and enable [DC]\r\n"
    "limit <M#|all> <0..255>      soft current limit (255=~2A, 0=off)\r\n"
    "mode open|closed             open-loop duty vs IS closed-loop [DC]\r\n"
    "rotate on|off                quadrature sine on M1..M4 (set amp first)\r\n"
    "freq <Hz>                    rotate freq 0.1..50 Hz\r\n"
    "amp <0..255>                 shared sine peak amplitude (default 0)\r\n"
    "rot_dir cw|ccw               Phase B lead (+90) or lag (-90)\r\n"
    "contour on|off               enable M1..M4 together (DC contour)\r\n"
    "program P1|P2|stop           preset patterns\r\n"
    "  P1 = M1..M4 ON (keep mag/dir)\r\n"
    "  P2 = M1..M4 ON @128 FWD\r\n"
    "  stop = all off\r\n"
    "json                         machine-readable status (ESP32 / UI)\r\n"
    "ping                         bridge heartbeat (replies PONG)\r\n"
    "\r\n"
    "Rotate: M1||M3 = Phase A (0 deg), M2||M4 = Phase B (+/-90 deg)\r\n"
    "DC writes to M1..M4 auto-stop rotate. Limits while rotating derate shared amp.\r\n"
    "Boot soft-limits all coils to ~2 A (limit … 0 disables).\r\n"
    "\r\n"
  );
}

static void CLI_PrintStatus(void)
{
  float fpwm = (float)timer_clock_hz(TIM4) /
               ((float)(htim4.Init.Prescaler + 1U) * (float)(PWM_ARR + 1U));
  uart_printf("mode=%s  contour=%u  rotate=%u  freq=%.2f Hz  amp=%u  amp_live=%u  rot_dir=%s\r\n",
              g_ctrl_mode == CTRL_OPEN ? "open" : "closed",
              (unsigned)g_contour_active,
              (unsigned)g_rotate_active,
              (double)g_rotate_freq_hz,
              (unsigned)g_rotate_amp,
              (unsigned)g_rotate_amp_live,
              g_rotate_dir == ROT_CW ? "cw" : "ccw");
  uart_printf("pwm≈%.1f kHz  sine_isr=%u Hz  coils=%d\r\n",
              fpwm / 1000.0f, (unsigned)SINE_UPDATE_HZ, NUM_COILS);
  for (int i = 0; i < NUM_COILS; i++) {
    CoilState *s = &coils[i];
    uart_printf("M%d en=%u mag=%3u dir=%s duty=%3u I=%.3fA lim=%.2fA\r\n",
                i + 1, s->enabled, s->magnitude,
                s->dir == DIR_FWD ? "+" : "-",
                s->duty_applied, s->i_meas_a, s->i_limit_a);
  }
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
    return;
  }
  if (!strcmp(cmd, "json")) {
    /* Sample ADCs first, then build one buffer and TX continuously. */
    float amps[NUM_COILS];
    for (int i = 0; i < NUM_COILS; i++) {
      amps[i] = Sense_ReadAmps((uint8_t)i);
    }
    static char js[896];
    int n = snprintf(js, sizeof(js),
                     "{\"ok\":1,\"mode\":\"%s\",\"contour\":%u,\"rotate\":%u,"
                     "\"freq\":%.2f,\"amp\":%u,\"amp_live\":%u,\"rot_dir\":\"%s\",\"n\":%d,\"coils\":[",
                     g_ctrl_mode == CTRL_OPEN ? "open" : "closed",
                     (unsigned)g_contour_active,
                     (unsigned)g_rotate_active,
                     (double)g_rotate_freq_hz,
                     (unsigned)g_rotate_amp,
                     (unsigned)g_rotate_amp_live,
                     g_rotate_dir == ROT_CW ? "cw" : "ccw",
                     NUM_COILS);
    for (int i = 0; i < NUM_COILS && n > 0 && n < (int)sizeof(js) - 120; i++) {
      CoilState *s = &coils[i];
      n += snprintf(js + n, sizeof(js) - (size_t)n,
                    "%s{\"id\":%d,\"en\":%u,\"mag\":%u,\"dir\":%d,\"duty\":%u,\"i\":%.3f,\"lim\":%.3f}",
                    i ? "," : "",
                    i + 1, s->enabled, s->magnitude,
                    s->dir == DIR_FWD ? 1 : -1,
                    s->duty_applied, amps[i], s->i_limit_a);
    }
    if (n > 0 && n < (int)sizeof(js) - 4) {
      n += snprintf(js + n, sizeof(js) - (size_t)n, "]}\r\n");
      uart_write(js);
    } else {
      uart_write("{\"ok\":0,\"err\":\"overflow\"}\r\n");
    }
    return;
  }
  if (!strcmp(cmd, "status")) {
    for (uint8_t i = 0; i < NUM_COILS; i++) Sense_ReadAmps(i);
    CLI_PrintStatus();
    return;
  }
  if (!strcmp(cmd, "sense")) {
    for (uint8_t i = 0; i < NUM_COILS; i++) {
      float a = Sense_ReadAmps(i);
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
    if (argc < 2) { uart_write("usage: rotate on|off\r\n"); return; }
    if (str_ieq(argv[1], "on")) {
      Rotate_Start();
      uart_printf("rotate ON freq=%.2f amp=%u dir=%s\r\n",
                  (double)g_rotate_freq_hz, (unsigned)g_rotate_amp,
                  g_rotate_dir == ROT_CW ? "cw" : "ccw");
    } else if (str_ieq(argv[1], "off")) {
      Rotate_Stop();
      uart_write("rotate OFF\r\n");
    } else {
      uart_write("usage: rotate on|off\r\n");
    }
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
    g_rotate_amp_live = g_rotate_amp;
    for (int i = 0; i < NUM_ROTATE_COILS && i < NUM_COILS; i++) {
      coils[i].magnitude = g_rotate_amp;
    }
    uart_printf("amp=%u\r\n", (unsigned)g_rotate_amp);
    return;
  }
  if (!strcmp(cmd, "rot_dir") || !strcmp(cmd, "rotdir")) {
    if (argc < 2) { uart_write("usage: rot_dir cw|ccw\r\n"); return; }
    if (str_ieq(argv[1], "cw") || str_ieq(argv[1], "+") || str_ieq(argv[1], "fwd")) {
      g_rotate_dir = ROT_CW;
      uart_write("rot_dir=cw\r\n");
    } else if (str_ieq(argv[1], "ccw") || str_ieq(argv[1], "-") || str_ieq(argv[1], "rev")) {
      g_rotate_dir = ROT_CCW;
      uart_write("rot_dir=ccw\r\n");
    } else {
      uart_write("usage: rot_dir cw|ccw\r\n");
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
        CLI_HandleLine(port->buf);
        if (port->echo) uart_write("> ");
        port->len = 0;
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
  MX_TIMx_Init();
  MX_TIM6_Init();
  MX_ADC1_Init();

  memset(coils, 0, sizeof(coils));
  for (int i = 0; i < NUM_COILS; i++) {
    coils[i].i_limit_a = I_FULLSCALE_A; /* default soft cap ≈ 2 A */
  }
  Rotate_RecalcPhaseInc();
  g_rotate_amp_live = g_rotate_amp;

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

  /* Onboard ST7735 status UI (SPI4 on PE10–PE14) */
  ST7735_Init();
  DisplayUI_Init();
  Display_PushStatus();

  uart_write_to(&cli_ports[0], "\r\n\r\nH743 coil driver ready. Type 'help'.\r\n> ");
  uart_write_to(&cli_ports[1], "BRIDGE_OK\r\n");
  last_loop_ms = HAL_GetTick();

  uint32_t led_ms = HAL_GetTick();
  uint32_t ui_ms = HAL_GetTick();
  while (1) {
    CLI_Poll();

    uint32_t now = HAL_GetTick();
    if ((now - last_loop_ms) >= (1000U / CURRENT_LOOP_HZ)) {
      last_loop_ms = now;
      CurrentLoop_Update();
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

/* ~10 kHz sine update for M1–M4 rotating field */
void TIM6_DAC_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim6);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance != TIM6 || !g_rotate_active) return;

  g_phase += g_phase_inc;
  uint8_t si = (uint8_t)(g_phase >> 24); /* top 8 bits → 256-entry LUT */
  int32_t s = SINE_LUT[si];
  int32_t c = SINE_LUT[(uint8_t)(si + 64U)]; /* +90° */
  if (g_rotate_dir == ROT_CCW) c = -c;

  uint8_t amp = g_rotate_amp_live;
  int16_t d_a = (int16_t)((s * (int32_t)amp) >> 15);
  int16_t d_b = (int16_t)((c * (int32_t)amp) >> 15);

  Coil_ApplySignedDuty(0, d_a); /* M1 Phase A */
  Coil_ApplySignedDuty(1, d_b); /* M2 Phase B */
  Coil_ApplySignedDuty(2, d_a); /* M3 Phase A */
  Coil_ApplySignedDuty(3, d_b); /* M4 Phase B */
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
