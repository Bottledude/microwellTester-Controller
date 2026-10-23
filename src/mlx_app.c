#include "mlx_app.h"
#include "coil_output.h"
#include "field_engine.h"
#include "fb_app.h"
#include "field_tilt.h"
#include "i2c_bus.h"
#include "pca9685.h"
#include "cli.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

Mlx90393 g_mlx_coil[NUM_COILS];
uint8_t  g_mlx_bind[NUM_COILS];
uint8_t  g_mlx_src[NUM_COILS][3] = {
  {0, 1, 2}, {0, 1, 2}, {0, 1, 2}, {0, 1, 2}
};
int8_t   g_mlx_sgn[NUM_COILS][3] = {
  {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}
};
float    g_mlx_b0_mt[NUM_COILS][3];
uint8_t  g_mlx_b0_ok[NUM_COILS];
int8_t   g_mlx_zsign[NUM_COILS] = {1, 1, 1, 1};
float    g_mlx_s_mt[NUM_COILS][3];
float    g_mlx_lab_mt[NUM_COILS][3];
float    g_mlx_babs_mt[NUM_COILS];
float    g_mlx_k[NUM_COILS] = {1.0f, 1.0f, 1.0f, 1.0f};
uint8_t  g_mlx_sat[NUM_COILS];
float    g_mlx_center_mt[3];
float    g_mlx_center_babs;
uint8_t  g_mlx_center_sat;
uint8_t  g_mlx_n_present;
uint8_t  g_mlx_en = 1;

/* Raw chip µT → sensor frame mT (x up, y left, z axial) via `mlx map`. */
static void Mlx_ChipToSensor(uint8_t coil, const float chip_ut[3],
                             uint8_t sub_b0, float s_mt[3])
{
  int a;
  for (a = 0; a < 3; a++) {
    uint8_t src = g_mlx_src[coil][a];
    if (src > 2u) src = 0;
    s_mt[a] = 0.001f * (float)g_mlx_sgn[coil][a] * chip_ut[src];
    if (sub_b0 && g_mlx_b0_ok[coil]) s_mt[a] -= g_mlx_b0_mt[coil][a];
  }
}

void Mlx_ReconstructCenter(void)
{
  uint8_t present[4];
  int i;
  for (i = 0; i < NUM_COILS; i++) {
    present[i] = g_mlx_coil[i].present;
    if (!present[i]) {
      g_mlx_lab_mt[i][0] = g_mlx_lab_mt[i][1] = g_mlx_lab_mt[i][2] = 0.0f;
      continue;
    }
    field_tilt_sensor_to_lab((uint8_t)i, g_mlx_zsign[i], g_mlx_s_mt[i],
                             g_mlx_lab_mt[i]);
  }
  field_tilt_center_from_sensors((const float (*)[3])g_mlx_s_mt, g_mlx_zsign,
                                 g_mlx_k, present,
                                 &g_mlx_center_mt[0], &g_mlx_center_mt[1],
                                 &g_mlx_center_mt[2]);
  g_mlx_center_babs = sqrtf(g_mlx_center_mt[0] * g_mlx_center_mt[0] +
                            g_mlx_center_mt[1] * g_mlx_center_mt[1] +
                            g_mlx_center_mt[2] * g_mlx_center_mt[2]);
  g_mlx_center_sat = 0;
  g_mlx_n_present = 0;
  for (i = 0; i < NUM_COILS; i++) {
    if (!g_mlx_coil[i].present) continue;
    g_mlx_n_present++;
    if (g_mlx_sat[i]) g_mlx_center_sat = 1;
  }
}

static void Mlx_ApplyCoilSample(uint8_t coil, const MlxSample *s)
{
  float chip_ut[3];
  float s_mt[3];

  if (!s || coil >= NUM_COILS) return;
  chip_ut[0] = s->ut_x;
  chip_ut[1] = s->ut_y;
  chip_ut[2] = s->ut_z;
  Mlx_ChipToSensor(coil, chip_ut, 1, s_mt);
  g_mlx_s_mt[coil][0] = s_mt[0];
  g_mlx_s_mt[coil][1] = s_mt[1];
  g_mlx_s_mt[coil][2] = s_mt[2];
  /* |B| is frame-invariant; lab vectors are rotated in Mlx_ReconstructCenter. */
  g_mlx_babs_mt[coil] = sqrtf(s_mt[0] * s_mt[0] + s_mt[1] * s_mt[1] +
                              s_mt[2] * s_mt[2]);
  g_mlx_sat[coil] = s->sat;
}

uint8_t mlx_parse_axis_tok(const char *t, uint8_t *src, int8_t *sgn)
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

static uint8_t mlx_avg_sensor_coil(uint8_t coil, int n, float s_mt[3])
{
  float acc[3] = {0, 0, 0};
  int i, got = 0;
  MlxSample s;
  if (coil >= NUM_COILS || !g_mlx_coil[coil].present) return 0;
  for (i = 0; i < n; i++) {
    if (!MLX90393_ReadBlocking(&g_mlx_coil[coil], &s)) continue;
    {
      float chip[3] = {s.ut_x, s.ut_y, s.ut_z};
      float sv[3];
      Mlx_ChipToSensor(coil, chip, 0, sv);
      acc[0] += sv[0];
      acc[1] += sv[1];
      acc[2] += sv[2];
    }
    g_mlx_sat[coil] = (uint8_t)(g_mlx_sat[coil] | s.sat);
    got++;
    PCA9685_Flush();
  }
  if (got == 0) return 0;
  s_mt[0] = acc[0] / (float)got;
  s_mt[1] = acc[1] / (float)got;
  s_mt[2] = acc[2] / (float)got;
  return 1;
}

void Mlx_PrintStatus(void)
{
  int i;
  uart_printf("mlx poll=%u present=%u/4 center Bx=%.3f By=%.3f Bz=%.3f |B|=%.3f%s\r\n",
              (unsigned)g_mlx_en, (unsigned)g_mlx_n_present,
              (double)g_mlx_center_mt[0], (double)g_mlx_center_mt[1],
              (double)g_mlx_center_mt[2], (double)g_mlx_center_babs,
              g_mlx_center_sat ? " SAT" : "");
  for (i = 0; i < NUM_COILS; i++) {
    uart_printf("  M%d %s addr=0x%02X sat=%u k=%.3f map=%c%c %c%c %c%c zsign=%c b0=%s\r\n",
                i + 1,
                g_mlx_coil[i].present ? "ok" : "MISSING",
                (unsigned)g_mlx_coil[i].addr,
                (unsigned)g_mlx_sat[i],
                (double)g_mlx_k[i],
                g_mlx_sgn[i][0] < 0 ? '-' : '+', "xyz"[g_mlx_src[i][0]],
                g_mlx_sgn[i][1] < 0 ? '-' : '+', "xyz"[g_mlx_src[i][1]],
                g_mlx_sgn[i][2] < 0 ? '-' : '+', "xyz"[g_mlx_src[i][2]],
                g_mlx_zsign[i] < 0 ? '-' : '+',
                g_mlx_b0_ok[i] ? "set" : "off");
    if (g_mlx_coil[i].present) {
      uart_printf("     chip_mT x=%.3f y=%.3f z=%.3f  axial=%.3f\r\n",
                  (double)g_mlx_s_mt[i][0], (double)g_mlx_s_mt[i][1],
                  (double)g_mlx_s_mt[i][2],
                  (double)((float)g_mlx_zsign[i] * g_mlx_s_mt[i][2]));
      uart_printf("     lab_mT Bx=%.3f By=%.3f Bz=%.3f |B|=%.3f\r\n",
                  (double)g_mlx_lab_mt[i][0], (double)g_mlx_lab_mt[i][1],
                  (double)g_mlx_lab_mt[i][2], (double)g_mlx_babs_mt[i]);
    }
  }
}

uint8_t mlx_take_offset(void)
{
  int i;
  uint8_t ok = 0;
  Coil_AllOff();
  i2c_delay_ms(MLX_CAL_SETTLE_MS);
  for (i = 0; i < NUM_COILS; i++) {
    float raw[3];
    g_mlx_b0_ok[i] = 0;
    g_mlx_sat[i] = 0;
    if (!g_mlx_coil[i].present) continue;
    if (!mlx_avg_sensor_coil((uint8_t)i, (int)MLX_CAL_AVG, raw)) continue;
    g_mlx_b0_mt[i][0] = raw[0];
    g_mlx_b0_mt[i][1] = raw[1];
    g_mlx_b0_mt[i][2] = raw[2];
    g_mlx_b0_ok[i] = 1;
    ok = 1;
  }
  Mlx_ReconstructCenter();
  return ok;
}

void mlx_try_init(void)
{
  uint8_t found[8];
  uint8_t n;
  int i;
  uint8_t assigned[NUM_COILS];

  memset(assigned, 0, sizeof(assigned));
  g_mlx_n_present = 0;
  for (i = 0; i < NUM_COILS; i++) {
    g_mlx_coil[i].present = 0;
    g_mlx_coil[i].addr = 0;
  }

  n = MLX90393_Scan(I2C_BUS_MLX, found, 8);
  if (n == 0) {
    found[0] = 0x18u;
    found[1] = 0x0Cu;
    n = 2;
  }

  for (i = 0; i < NUM_COILS; i++) {
    uint8_t addr = g_mlx_bind[i];
    int k;
    if (addr == 0) {
      for (k = 0; k < (int)n; k++) {
        uint8_t used = 0;
        int j;
        for (j = 0; j < i; j++) {
          if (assigned[j] && g_mlx_coil[j].addr == found[k]) used = 1;
        }
        if (!used) {
          addr = found[k];
          break;
        }
      }
    }
    if (addr == 0) continue;
    if (MLX90393_Init(&g_mlx_coil[i], I2C_BUS_MLX, addr)) {
      assigned[i] = 1;
      g_mlx_n_present++;
    }
  }
  Mlx_ReconstructCenter();
}

void Mlx_PollAll(void)
{
  static uint32_t start_ms;
  /* Feedback needs all four chips from one trigger, tagged with its phase. */
  static uint32_t trig_phase;
  static uint8_t  trig_steady;
  static uint8_t  trig_mask;
  static uint8_t  got_mask;
  static uint8_t  set_sat;
  int i;
  uint8_t fresh = 0;
  uint32_t now = i2c_millis();
  for (i = 0; i < NUM_COILS; i++) {
    MlxSample s;
    if (!g_mlx_coil[i].present) continue;
    if (MLX90393_ReadIfReady(&g_mlx_coil[i], &s)) {
      Mlx_ApplyCoilSample((uint8_t)i, &s);
      fresh = 1;
      got_mask |= (uint8_t)(1u << i);
      if (s.sat) set_sat = 1;
    }
  }
  if (fresh) Mlx_ReconstructCenter();
  if (trig_mask == 0x0Fu && (got_mask & trig_mask) == trig_mask) {
    float m[4];
    for (i = 0; i < NUM_COILS; i++) {
      /* Coil i's centre contribution, exactly as the reconstruction uses it. */
      float k = (g_mlx_k[i] != 0.0f) ? g_mlx_k[i] : 1.0f;
      m[i] = (float)g_mlx_zsign[i] * g_mlx_s_mt[i][2] / k;
    }
    Fb_OnMlxSet(trig_phase, trig_steady, m, set_sat);
    trig_mask = 0;
  }
  if ((now - start_ms) >= (1000u / MLX_POLL_HZ)) {
    uint8_t started = 0;
    uint32_t ph = g_phase;
    uint8_t steady = Field_FbSteady();
    start_ms = now;
    for (i = 0; i < NUM_COILS; i++) {
      if (g_mlx_coil[i].present && !g_mlx_coil[i].sm_tick) {
        if (MLX90393_StartMeasure(&g_mlx_coil[i])) started |= (uint8_t)(1u << i);
      }
    }
    if (started) {
      trig_phase = ph;
      trig_steady = steady;
      trig_mask = started;
      got_mask = 0;
      set_sat = 0;
    }
  }
}
