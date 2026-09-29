/**
 * @file st7735.h
 * @brief WeAct Mini onboard 0.96" ST7735 (160x80) over SPI4.
 *
 * Pins (board fixed):
 *   SPI4 SCK  PE12
 *   SPI4 MOSI PE14
 *   CS        PE11
 *   DC        PE13
 *   BL        PE10 (active low)
 */
#ifndef ST7735_H
#define ST7735_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ST7735_WIDTH   160
#define ST7735_HEIGHT   80

/* RGB565 helpers */
#define ST7735_RGB(r, g, b) \
  ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

#define ST7735_BLACK   0x0000
#define ST7735_WHITE   0xFFFF
#define ST7735_RED     0xF800
#define ST7735_GREEN   0x07E0
#define ST7735_BLUE    0x001F
#define ST7735_YELLOW  0xFFE0
#define ST7735_CYAN    0x07FF
#define ST7735_GRAY    0x8410
#define ST7735_DKGRAY  0x4208
#define ST7735_ORANGE  0xFD20

void ST7735_Init(void);
void ST7735_FillScreen(uint16_t color);
void ST7735_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg);
void ST7735_DrawString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg);

#ifdef __cplusplus
}
#endif

#endif /* ST7735_H */
