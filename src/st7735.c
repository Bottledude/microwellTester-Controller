/**
 * @file st7735.c
 * @brief Minimal ST7735 driver for WeAct Mini H743 onboard 160x80 panel (SPI4).
 */
#include "st7735.h"

#include "stm32h7xx_hal.h"

#include <string.h>

/* Zephyr board DTS: x-offset=1, y-offset=26, inversion-on, rgb-is-inverted */
#define COL_OFFSET  1U
#define ROW_OFFSET  26U

#define PIN_CS   GPIO_PIN_11
#define PIN_DC   GPIO_PIN_13
#define PIN_BL   GPIO_PIN_10
#define PIN_SCK  GPIO_PIN_12
#define PIN_MOSI GPIO_PIN_14

#define CS_LOW()   HAL_GPIO_WritePin(GPIOE, PIN_CS, GPIO_PIN_RESET)
#define CS_HIGH()  HAL_GPIO_WritePin(GPIOE, PIN_CS, GPIO_PIN_SET)
#define DC_CMD()   HAL_GPIO_WritePin(GPIOE, PIN_DC, GPIO_PIN_RESET)
#define DC_DATA()  HAL_GPIO_WritePin(GPIOE, PIN_DC, GPIO_PIN_SET)
#define BL_ON()    HAL_GPIO_WritePin(GPIOE, PIN_BL, GPIO_PIN_RESET) /* active low */
#define BL_OFF()   HAL_GPIO_WritePin(GPIOE, PIN_BL, GPIO_PIN_SET)

/* ST7735 commands */
#define ST_NOP     0x00
#define ST_SWRESET 0x01
#define ST_SLPOUT  0x11
#define ST_NORON   0x13
#define ST_INVOFF  0x20
#define ST_INVON   0x21
#define ST_DISPOFF 0x28
#define ST_DISPON  0x29
#define ST_CASET   0x2A
#define ST_RASET   0x2B
#define ST_RAMWR   0x2C
#define ST_MADCTL  0x36
#define ST_COLMOD  0x3A
#define ST_FRMCTR1 0xB1
#define ST_FRMCTR2 0xB2
#define ST_FRMCTR3 0xB3
#define ST_INVCTR  0xB4
#define ST_PWCTR1  0xC0
#define ST_PWCTR2  0xC1
#define ST_PWCTR3  0xC2
#define ST_PWCTR4  0xC3
#define ST_PWCTR5  0xC4
#define ST_VMCTR1  0xC5
#define ST_GMCTRP1 0xE0
#define ST_GMCTRN1 0xE1

/* MADCTL: MV|MY + BGR (landscape 160x80) */
#define MADCTL_LANDSCAPE_BGR  0xA8U

static SPI_HandleTypeDef hspi4;

/* 6x8 font, ASCII 0x20..0x7F — column-major, LSB at top of glyph */
static const uint8_t FONT6X8[][5] = {
  {0x00,0x00,0x00,0x00,0x00}, /*   */
  {0x00,0x00,0x5F,0x00,0x00}, /* ! */
  {0x00,0x07,0x00,0x07,0x00}, /* " */
  {0x14,0x7F,0x14,0x7F,0x14}, /* # */
  {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
  {0x23,0x13,0x08,0x64,0x62}, /* % */
  {0x36,0x49,0x55,0x22,0x50}, /* & */
  {0x00,0x05,0x03,0x00,0x00}, /* ' */
  {0x00,0x1C,0x22,0x41,0x00}, /* ( */
  {0x00,0x41,0x22,0x1C,0x00}, /* ) */
  {0x08,0x2A,0x1C,0x2A,0x08}, /* * */
  {0x08,0x08,0x3E,0x08,0x08}, /* + */
  {0x00,0x50,0x30,0x00,0x00}, /* , */
  {0x08,0x08,0x08,0x08,0x08}, /* - */
  {0x00,0x60,0x60,0x00,0x00}, /* . */
  {0x20,0x10,0x08,0x04,0x02}, /* / */
  {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
  {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
  {0x42,0x61,0x51,0x49,0x46}, /* 2 */
  {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
  {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
  {0x27,0x45,0x45,0x45,0x39}, /* 5 */
  {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
  {0x01,0x71,0x09,0x05,0x03}, /* 7 */
  {0x36,0x49,0x49,0x49,0x36}, /* 8 */
  {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
  {0x00,0x36,0x36,0x00,0x00}, /* : */
  {0x00,0x56,0x36,0x00,0x00}, /* ; */
  {0x00,0x08,0x14,0x22,0x41}, /* < */
  {0x14,0x14,0x14,0x14,0x14}, /* = */
  {0x41,0x22,0x14,0x08,0x00}, /* > */
  {0x02,0x01,0x51,0x09,0x06}, /* ? */
  {0x32,0x49,0x79,0x41,0x3E}, /* @ */
  {0x7E,0x11,0x11,0x11,0x7E}, /* A */
  {0x7F,0x49,0x49,0x49,0x36}, /* B */
  {0x3E,0x41,0x41,0x41,0x22}, /* C */
  {0x7F,0x41,0x41,0x22,0x1C}, /* D */
  {0x7F,0x49,0x49,0x49,0x41}, /* E */
  {0x7F,0x09,0x09,0x01,0x01}, /* F */
  {0x3E,0x41,0x41,0x51,0x32}, /* G */
  {0x7F,0x08,0x08,0x08,0x7F}, /* H */
  {0x00,0x41,0x7F,0x41,0x00}, /* I */
  {0x20,0x40,0x41,0x3F,0x01}, /* J */
  {0x7F,0x08,0x14,0x22,0x41}, /* K */
  {0x7F,0x40,0x40,0x40,0x40}, /* L */
  {0x7F,0x02,0x04,0x02,0x7F}, /* M */
  {0x7F,0x04,0x08,0x10,0x7F}, /* N */
  {0x3E,0x41,0x41,0x41,0x3E}, /* O */
  {0x7F,0x09,0x09,0x09,0x06}, /* P */
  {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
  {0x7F,0x09,0x19,0x29,0x46}, /* R */
  {0x46,0x49,0x49,0x49,0x31}, /* S */
  {0x01,0x01,0x7F,0x01,0x01}, /* T */
  {0x3F,0x40,0x40,0x40,0x3F}, /* U */
  {0x1F,0x20,0x40,0x20,0x1F}, /* V */
  {0x7F,0x20,0x18,0x20,0x7F}, /* W */
  {0x63,0x14,0x08,0x14,0x63}, /* X */
  {0x03,0x04,0x78,0x04,0x03}, /* Y */
  {0x61,0x51,0x49,0x45,0x43}, /* Z */
  {0x00,0x7F,0x41,0x41,0x00}, /* [ */
  {0x02,0x04,0x08,0x10,0x20}, /* \ */
  {0x00,0x41,0x41,0x7F,0x00}, /* ] */
  {0x04,0x02,0x01,0x02,0x04}, /* ^ */
  {0x40,0x40,0x40,0x40,0x40}, /* _ */
  {0x00,0x01,0x02,0x04,0x00}, /* ` */
  {0x20,0x54,0x54,0x54,0x78}, /* a */
  {0x7F,0x48,0x44,0x44,0x38}, /* b */
  {0x38,0x44,0x44,0x44,0x20}, /* c */
  {0x38,0x44,0x44,0x48,0x7F}, /* d */
  {0x38,0x54,0x54,0x54,0x18}, /* e */
  {0x08,0x7E,0x09,0x01,0x02}, /* f */
  {0x08,0x14,0x54,0x54,0x3C}, /* g */
  {0x7F,0x08,0x04,0x04,0x78}, /* h */
  {0x00,0x44,0x7D,0x40,0x00}, /* i */
  {0x20,0x40,0x44,0x3D,0x00}, /* j */
  {0x7F,0x10,0x28,0x44,0x00}, /* k */
  {0x00,0x41,0x7F,0x40,0x00}, /* l */
  {0x7C,0x04,0x18,0x04,0x78}, /* m */
  {0x7C,0x08,0x04,0x04,0x78}, /* n */
  {0x38,0x44,0x44,0x44,0x38}, /* o */
  {0x7C,0x14,0x14,0x14,0x08}, /* p */
  {0x08,0x14,0x14,0x18,0x7C}, /* q */
  {0x7C,0x08,0x04,0x04,0x08}, /* r */
  {0x48,0x54,0x54,0x54,0x24}, /* s */
  {0x04,0x3F,0x44,0x40,0x20}, /* t */
  {0x3C,0x40,0x40,0x20,0x7C}, /* u */
  {0x1C,0x20,0x40,0x20,0x1C}, /* v */
  {0x3C,0x40,0x30,0x40,0x3C}, /* w */
  {0x44,0x28,0x10,0x28,0x44}, /* x */
  {0x0C,0x50,0x50,0x50,0x3C}, /* y */
  {0x44,0x64,0x54,0x4C,0x44}, /* z */
  {0x00,0x08,0x36,0x41,0x00}, /* { */
  {0x00,0x00,0x7F,0x00,0x00}, /* | */
  {0x00,0x41,0x36,0x08,0x00}, /* } */
  {0x08,0x04,0x08,0x10,0x08}, /* ~ */
  {0x00,0x00,0x00,0x00,0x00}, /* DEL placeholder */
};

static void spi_write(const uint8_t *data, uint16_t len)
{
  if (len == 0) return;
  (void)HAL_SPI_Transmit(&hspi4, (uint8_t *)data, len, 50);
}

static void write_cmd(uint8_t cmd)
{
  DC_CMD();
  CS_LOW();
  spi_write(&cmd, 1);
  CS_HIGH();
}

static void write_data(const uint8_t *data, uint16_t len)
{
  DC_DATA();
  CS_LOW();
  spi_write(data, len);
  CS_HIGH();
}

static void write_data8(uint8_t v)
{
  write_data(&v, 1);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  uint8_t buf[4];
  uint16_t xs = (uint16_t)(x0 + COL_OFFSET);
  uint16_t xe = (uint16_t)(x1 + COL_OFFSET);
  uint16_t ys = (uint16_t)(y0 + ROW_OFFSET);
  uint16_t ye = (uint16_t)(y1 + ROW_OFFSET);

  write_cmd(ST_CASET);
  buf[0] = (uint8_t)(xs >> 8);
  buf[1] = (uint8_t)(xs & 0xFF);
  buf[2] = (uint8_t)(xe >> 8);
  buf[3] = (uint8_t)(xe & 0xFF);
  write_data(buf, 4);

  write_cmd(ST_RASET);
  buf[0] = (uint8_t)(ys >> 8);
  buf[1] = (uint8_t)(ys & 0xFF);
  buf[2] = (uint8_t)(ye >> 8);
  buf[3] = (uint8_t)(ye & 0xFF);
  write_data(buf, 4);

  write_cmd(ST_RAMWR);
}

static void mx_spi4_init(void)
{
  GPIO_InitTypeDef g = {0};
  RCC_PeriphCLKInitTypeDef per = {0};

  /* SPI4 kernel clock = APB2 (D2PCLK2) */
  per.PeriphClockSelection = RCC_PERIPHCLK_SPI4;
  per.Spi45ClockSelection = RCC_SPI45CLKSOURCE_D2PCLK2;
  if (HAL_RCCEx_PeriphCLKConfig(&per) != HAL_OK) {
    /* Non-fatal for bring-up; HAL_SPI_Init may still succeed with reset default */
  }

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_SPI4_CLK_ENABLE();

  /* CS / DC / BL as GPIO */
  g.Pin = PIN_CS | PIN_DC | PIN_BL;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOE, &g);
  CS_HIGH();
  DC_CMD();
  BL_OFF();

  /* SPI4 SCK + MOSI */
  g.Pin = PIN_SCK | PIN_MOSI;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF5_SPI4;
  HAL_GPIO_Init(GPIOE, &g);

  hspi4.Instance = SPI4;
  hspi4.Init.Mode = SPI_MODE_MASTER;
  hspi4.Init.Direction = SPI_DIRECTION_2LINES_TXONLY;
  hspi4.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi4.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi4.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi4.Init.NSS = SPI_NSS_SOFT;
  hspi4.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8; /* ~15 MHz @ 120 MHz APB */
  hspi4.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi4.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi4.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi4.Init.CRCPolynomial = 0x0;
  hspi4.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi4.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi4.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi4.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi4.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi4.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi4.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi4.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi4.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
  hspi4.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi4) != HAL_OK) {
    /* Leave UI dark; coil control still works */
  }
}

void ST7735_Init(void)
{
  mx_spi4_init();

  write_cmd(ST_SWRESET);
  HAL_Delay(150);
  write_cmd(ST_SLPOUT);
  HAL_Delay(120);

  write_cmd(ST_FRMCTR1);
  { uint8_t d[] = {0x01, 0x2C, 0x2D}; write_data(d, 3); }
  write_cmd(ST_FRMCTR2);
  { uint8_t d[] = {0x01, 0x2C, 0x2D}; write_data(d, 3); }
  write_cmd(ST_FRMCTR3);
  { uint8_t d[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}; write_data(d, 6); }

  write_cmd(ST_INVCTR);
  write_data8(0x07);

  write_cmd(ST_PWCTR1);
  { uint8_t d[] = {0xA2, 0x02, 0x84}; write_data(d, 3); }
  write_cmd(ST_PWCTR2);
  write_data8(0xC5);
  write_cmd(ST_PWCTR3);
  { uint8_t d[] = {0x0A, 0x00}; write_data(d, 2); }
  write_cmd(ST_PWCTR4);
  { uint8_t d[] = {0x8A, 0x2A}; write_data(d, 2); }
  write_cmd(ST_PWCTR5);
  { uint8_t d[] = {0x8A, 0xEE}; write_data(d, 2); }
  write_cmd(ST_VMCTR1);
  write_data8(0x0E);

  write_cmd(ST_INVON);

  write_cmd(ST_MADCTL);
  write_data8(MADCTL_LANDSCAPE_BGR);

  write_cmd(ST_COLMOD);
  write_data8(0x05); /* 16-bit */

  write_cmd(ST_GMCTRP1);
  {
    uint8_t d[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                   0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
    write_data(d, 16);
  }
  write_cmd(ST_GMCTRN1);
  {
    uint8_t d[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                   0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};
    write_data(d, 16);
  }

  write_cmd(ST_NORON);
  HAL_Delay(10);
  write_cmd(ST_DISPON);
  HAL_Delay(10);

  ST7735_FillScreen(ST7735_BLACK);
  BL_ON();
}

void ST7735_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if (w == 0 || h == 0) return;
  if (x >= ST7735_WIDTH || y >= ST7735_HEIGHT) return;
  if (x + w > ST7735_WIDTH) w = (uint16_t)(ST7735_WIDTH - x);
  if (y + h > ST7735_HEIGHT) h = (uint16_t)(ST7735_HEIGHT - y);

  set_window(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));

  uint8_t hi = (uint8_t)(color >> 8);
  uint8_t lo = (uint8_t)(color & 0xFF);
  uint32_t pixels = (uint32_t)w * (uint32_t)h;

  DC_DATA();
  CS_LOW();
  while (pixels--) {
    spi_write(&hi, 1);
    spi_write(&lo, 1);
  }
  CS_HIGH();
}

void ST7735_FillScreen(uint16_t color)
{
  ST7735_FillRect(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

void ST7735_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg)
{
  if (c < 0x20 || c > 0x7F) c = '?';
  const uint8_t *glyph = FONT6X8[(unsigned)c - 0x20];
  uint8_t hi_fg = (uint8_t)(fg >> 8), lo_fg = (uint8_t)(fg & 0xFF);
  uint8_t hi_bg = (uint8_t)(bg >> 8), lo_bg = (uint8_t)(bg & 0xFF);
  uint8_t buf[6 * 8 * 2];
  uint16_t n = 0;

  /* Row-major for set_window(x..x+5, y..y+7) */
  for (uint16_t row = 0; row < 8; row++) {
    for (uint16_t col = 0; col < 5; col++) {
      uint8_t on = glyph[col] & (1U << row);
      buf[n++] = on ? hi_fg : hi_bg;
      buf[n++] = on ? lo_fg : lo_bg;
    }
    buf[n++] = hi_bg;
    buf[n++] = lo_bg; /* spacer column */
  }

  set_window(x, y, (uint16_t)(x + 5), (uint16_t)(y + 7));
  write_data(buf, n);
}

void ST7735_DrawString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{
  if (!s) return;
  while (*s) {
    if (x + 6 > ST7735_WIDTH) break;
    ST7735_DrawChar(x, y, *s++, fg, bg);
    x = (uint16_t)(x + 6);
  }
}
