/**
 * @file display_ui.c
 * @brief Live per-coil status on the 160x80 ST7735.
 */
#include "display_ui.h"
#include "st7735.h"

#include <stdio.h>
#include <string.h>

#define MAX_ROWS 6
#define HEADER_Y 0
#define ROW0_Y   10
#define ROW_H    11
#define COL_X    0

typedef struct {
  char line[28];
  uint16_t fg;
} RowCache;

static char s_header[28];
static RowCache s_rows[MAX_ROWS];
static int s_nrows;
static uint8_t s_inited;

static void draw_header(const char *text)
{
  ST7735_FillRect(0, HEADER_Y, ST7735_WIDTH, 9, ST7735_DKGRAY);
  ST7735_DrawString(2, HEADER_Y + 1, text, ST7735_CYAN, ST7735_DKGRAY);
}

static void draw_row(int i, const char *text, uint16_t fg)
{
  uint16_t y = (uint16_t)(ROW0_Y + i * ROW_H);
  ST7735_FillRect(0, y, ST7735_WIDTH, ROW_H - 1, ST7735_BLACK);
  ST7735_DrawString(COL_X, y, text, fg, ST7735_BLACK);
}

void DisplayUI_Init(void)
{
  memset(s_header, 0, sizeof(s_header));
  memset(s_rows, 0, sizeof(s_rows));
  s_nrows = 0;
  s_inited = 1;

  ST7735_FillScreen(ST7735_BLACK);
  draw_header("CoilCtrl boot...");
  ST7735_DrawString(2, 28, "waiting for coils", ST7735_GRAY, ST7735_BLACK);
}

void DisplayUI_Update(const char *mode, uint8_t contour,
                      uint8_t rotate, float freq_hz, uint8_t amp,
                      const char *field_tag,
                      const DisplayCoilSnap *coils, int n,
                      const DisplayMlxSnap *mlx)
{
  if (!s_inited || !coils) return;
  if (n < 0) n = 0;
  if (n > MAX_ROWS - 1) n = MAX_ROWS - 1; /* leave a row for measured B */

  char hdr[28];
  if (rotate) {
    if (mlx && mlx->present) {
      snprintf(hdr, sizeof(hdr), "%s %.2fHz %.1fmT",
               (field_tag && field_tag[0]) ? field_tag : "fld",
               (double)freq_hz, (double)mlx->babs_mt);
    } else {
      snprintf(hdr, sizeof(hdr), "%s %.1fHz A%u",
               (field_tag && field_tag[0]) ? field_tag : "fld",
               (double)freq_hz, (unsigned)amp);
    }
  } else if (mlx && mlx->present) {
    snprintf(hdr, sizeof(hdr), "%-4s |B|%.1fmT",
             mode ? mode : "?", (double)mlx->babs_mt);
  } else {
    snprintf(hdr, sizeof(hdr), "%-6s c:%u", mode ? mode : "?", (unsigned)contour);
  }
  if (strcmp(hdr, s_header) != 0) {
    strncpy(s_header, hdr, sizeof(s_header) - 1);
    s_header[sizeof(s_header) - 1] = '\0';
    draw_header(s_header);
  }

  /* Coil rows + one measured-B row. */
  {
    int total = n + 1;
    if (total > MAX_ROWS) total = MAX_ROWS;
    for (int i = total; i < s_nrows; i++) {
      uint16_t y = (uint16_t)(ROW0_Y + i * ROW_H);
      ST7735_FillRect(0, y, ST7735_WIDTH, ROW_H - 1, ST7735_BLACK);
      s_rows[i].line[0] = '\0';
    }
  }

  for (int i = 0; i < n; i++) {
    const DisplayCoilSnap *c = &coils[i];
    char line[28];
    uint16_t fg;

    if (c->enabled) {
      char dir = (c->dir < 0) ? '-' : '+';
      /* M1 ON 180 +0.42A  — fit 160/6 ≈ 26 chars */
      snprintf(line, sizeof(line), "M%d ON %3u %c%.2fA",
               i + 1, (unsigned)c->magnitude, dir, (double)c->i_meas_a);
      fg = ST7735_GREEN;
      if (c->i_limit_a > 0.0f && c->i_meas_a >= (c->i_limit_a * 0.9f)) {
        fg = ST7735_ORANGE;
      }
    } else {
      snprintf(line, sizeof(line), "M%d OFF", i + 1);
      fg = ST7735_GRAY;
    }

    if (strcmp(line, s_rows[i].line) != 0 || fg != s_rows[i].fg) {
      strncpy(s_rows[i].line, line, sizeof(s_rows[i].line) - 1);
      s_rows[i].line[sizeof(s_rows[i].line) - 1] = '\0';
      s_rows[i].fg = fg;
      draw_row(i, line, fg);
    }
  }

  /* Extra row: measured lab B in mT (offset-subtracted). */
  {
    char line[28];
    uint16_t fg;
    int i = n;
    if (mlx && mlx->present) {
      snprintf(line, sizeof(line), "B%+.1f%+.1f%+.1f %u/4%s",
               (double)mlx->bx_mt, (double)mlx->by_mt, (double)mlx->bz_mt,
               (unsigned)mlx->n_present,
               mlx->sat ? "!" : "");
      fg = mlx->sat ? ST7735_ORANGE : ST7735_GRAY;
    } else {
      snprintf(line, sizeof(line), "mlx 0/4");
      fg = ST7735_GRAY;
    }
    if (i < MAX_ROWS && (strcmp(line, s_rows[i].line) != 0 || fg != s_rows[i].fg)) {
      strncpy(s_rows[i].line, line, sizeof(s_rows[i].line) - 1);
      s_rows[i].line[sizeof(s_rows[i].line) - 1] = '\0';
      s_rows[i].fg = fg;
      draw_row(i, line, fg);
    }
    if (s_nrows < i + 1) s_nrows = i + 1;
  }
}
