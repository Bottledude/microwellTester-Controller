#include "cli.h"
#include "coil_output.h"
#include "field_engine.h"
#include "mlx_app.h"
#include "fb_app.h"
#include "pca9685.h"
#include "platform_lock.h"
#include "serial_io.h"
#include "config.h"
#include "field_tilt.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

uint8_t g_teleplot_en;

static char *g_reply_buf;
static size_t g_reply_cap;
static size_t g_reply_len;
static uint8_t g_reply_to_serial = 1;
static char g_uart_line[CLI_LINE_MAX];
static uint16_t g_uart_len;
static uint8_t g_uart_echo = 1;

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

void uart_write(const char *s)
{
  if (!s) return;
  if (g_reply_buf && g_reply_cap) {
    while (*s && g_reply_len + 1 < g_reply_cap) {
      g_reply_buf[g_reply_len++] = *s++;
    }
    if (g_reply_len < g_reply_cap) g_reply_buf[g_reply_len] = '\0';
    else g_reply_buf[g_reply_cap - 1] = '\0';
    return;
  }
  if (g_reply_to_serial) serial_write_bytes(s);
}

void uart_printf(const char *fmt, ...)
{
  char tmp[384];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  uart_write(tmp);
}

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

static void lab_from_pair(int16_t b1, int16_t b2, float *horiz, float *vert)
{
  float s1 = (float)b1;
  float s2 = (float)b2;
  *horiz = -FIELD_CH * (s1 + s2);
  *vert  =  FIELD_CV * (s1 - s2);
}

void Teleplot_Emit(void)
{
  int16_t sd[NUM_COILS];
  uint8_t pr[NUM_COILS], pl[NUM_COILS];
  uint8_t amp_live;
  float bx, by_xy, bz, by_yz, by, babs;
  char line[48];
  int i;

  if (!g_teleplot_en) return;

  field_irq_lock();
  memcpy(sd, (const void *)g_signed_d, sizeof(sd));
  memcpy(pr, g_pwm_r, sizeof(pr));
  memcpy(pl, g_pwm_l, sizeof(pl));
  amp_live = g_rotate_amp_live;
  field_irq_unlock();

  if (!g_rotate_active) {
    for (i = 0; i < NUM_COILS; i++) {
      if (pr[i]) sd[i] = (int16_t)pr[i];
      else if (pl[i]) sd[i] = (int16_t)(-(int16_t)pl[i]);
      else sd[i] = 0;
    }
  }

  lab_from_pair(sd[1], sd[3], &bx, &by_xy);
  lab_from_pair(sd[0], sd[2], &bz, &by_yz);
  by = by_xy + by_yz;
  babs = sqrtf(bx * bx + by * by + bz * bz);

  for (i = 0; i < NUM_COILS; i++) {
    snprintf(line, sizeof(line), ">M%d_signed:%d\n", i + 1, (int)sd[i]);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">M%d_rpwm:%u\n", i + 1, (unsigned)pr[i]);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">M%d_lpwm:%u\n", i + 1, (unsigned)pl[i]);
    serial_write_bytes(line);
  }
  snprintf(line, sizeof(line), ">amp_live:%u\n", (unsigned)amp_live);
  serial_write_bytes(line);
  snprintf(line, sizeof(line), ">Bx:%.2f\n", (double)bx);
  serial_write_bytes(line);
  snprintf(line, sizeof(line), ">By:%.2f\n", (double)by);
  serial_write_bytes(line);
  snprintf(line, sizeof(line), ">Bz:%.2f\n", (double)bz);
  serial_write_bytes(line);
  snprintf(line, sizeof(line), ">Babs:%.2f\n", (double)babs);
  serial_write_bytes(line);
  {
    float sg = g_tilt_sin_g;
    float cg = g_tilt_cos_g;
    float gdeg = g_tilt_gamma_live;
    float scale = g_tilt_scale;
    float bh = -bx * sg + bz * cg;
    float bv = by;
    float bn = bx * cg + bz * sg;
    snprintf(line, sizeof(line), ">gamma:%.2f\n", (double)gdeg);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Bh:%.2f\n", (double)bh);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Bv:%.2f\n", (double)bv);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Bn:%.2f\n", (double)bn);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">tilt_scale:%.4f\n", (double)scale);
    serial_write_bytes(line);
  }
  snprintf(line, sizeof(line), ">Bxy:%.2f:%.2f|xy\n", (double)bx, (double)by);
  serial_write_bytes(line);
  snprintf(line, sizeof(line), ">Bxz:%.2f:%.2f|xy\n", (double)bz, (double)bx);
  serial_write_bytes(line);
  snprintf(line, sizeof(line), ">Byz:%.2f:%.2f|xy\n", (double)bz, (double)by);
  serial_write_bytes(line);
  if (g_mlx_n_present) {
    snprintf(line, sizeof(line), ">Bx_c:%.3f\n", (double)g_mlx_center_mt[0]);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">By_c:%.3f\n", (double)g_mlx_center_mt[1]);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Bz_c:%.3f\n", (double)g_mlx_center_mt[2]);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Babs_c:%.3f\n", (double)g_mlx_center_babs);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Bx_m:%.3f\n", (double)g_mlx_center_mt[0]);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">By_m:%.3f\n", (double)g_mlx_center_mt[1]);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Bz_m:%.3f\n", (double)g_mlx_center_mt[2]);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Babs_m:%.3f\n", (double)g_mlx_center_babs);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Bxy_m:%.3f:%.3f|xy\n",
             (double)g_mlx_center_mt[0], (double)g_mlx_center_mt[1]);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Byz_m:%.3f:%.3f|xy\n",
             (double)g_mlx_center_mt[2], (double)g_mlx_center_mt[1]);
    serial_write_bytes(line);
    snprintf(line, sizeof(line), ">Bxz_m:%.3f:%.3f|xy\n",
             (double)g_mlx_center_mt[2], (double)g_mlx_center_mt[0]);
    serial_write_bytes(line);
    for (i = 0; i < NUM_COILS; i++) {
      if (!g_mlx_coil[i].present) continue;
      snprintf(line, sizeof(line), ">M%d_bx_m:%.3f\n", i + 1,
               (double)g_mlx_lab_mt[i][0]);
      serial_write_bytes(line);
      snprintf(line, sizeof(line), ">M%d_by_m:%.3f\n", i + 1,
               (double)g_mlx_lab_mt[i][1]);
      serial_write_bytes(line);
      snprintf(line, sizeof(line), ">M%d_bz_m:%.3f\n", i + 1,
               (double)g_mlx_lab_mt[i][2]);
      serial_write_bytes(line);
    }
    {
      /* Expected centre B (mT) at the same chip-set phase: overlay on *_c. */
      float e[3];
      if (Fb_Expected(e)) {
        snprintf(line, sizeof(line), ">Bx_e:%.3f\n", (double)e[0]);
        serial_write_bytes(line);
        snprintf(line, sizeof(line), ">By_e:%.3f\n", (double)e[1]);
        serial_write_bytes(line);
        snprintf(line, sizeof(line), ">Bz_e:%.3f\n", (double)e[2]);
        serial_write_bytes(line);
        snprintf(line, sizeof(line), ">Babs_e:%.3f\n",
                 (double)sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]));
        serial_write_bytes(line);
        snprintf(line, sizeof(line), ">Bxy_e:%.3f:%.3f|xy\n", (double)e[0], (double)e[1]);
        serial_write_bytes(line);
        snprintf(line, sizeof(line), ">Byz_e:%.3f:%.3f|xy\n", (double)e[2], (double)e[1]);
        serial_write_bytes(line);
        snprintf(line, sizeof(line), ">Bxz_e:%.3f:%.3f|xy\n", (double)e[2], (double)e[0]);
        serial_write_bytes(line);
        snprintf(line, sizeof(line), ">fb_err:%.2f\n", (double)(100.0f * g_fb.err));
        serial_write_bytes(line);
      }
    }
  }
}

static void CLI_PrintHelp(void)
{
  uart_write(
    "\r\n=== ESP32 BTS7960 coil driver ===\r\n"
    "help                         show this help\r\n"
    "status                       show coil states\r\n"
    "on <M1..M4> [..]             enable coil(s) [DC]\r\n"
    "off <M1..M4|all> [..]        disable coil(s)\r\n"
    "set <M#|all> <0..255>        magnitude (open-loop duty) [DC]\r\n"
    "dir <M#|all> <+|->           B-field / current direction [DC]\r\n"
    "drive <M#> <0..255> <+|->    set mag+dir and enable [DC]\r\n"
    "rotate on|off [Hz] [0..255]  Y-axis rotate in XZ (M1=M3 cos, M2=M4 sin)\r\n"
    "revolve x|z|tilt [Hz] [amp]  4-coil pure-sine 30 deg law; x=γ0 YZ, z=γ90 XY\r\n"
    "revolve off                  stop AC field (same ramp as rotate off)\r\n"
    "axis y|x|z|tilt              select program without start (y=rotate, x/z/tilt=revolve)\r\n"
    "gamma <0..180>               tilt-axis angle in deg from +X toward +Z (tilt mode)\r\n"
    "freq <Hz>                    AC field freq 0.01..50 Hz\r\n"
    "amp <0..255>                 shared AC amp (revolve: |B|=amp*rev_gain, 255=max circle)\r\n"
    "rev_hv <h%> <v%>             Ah=amp*gain*h/100 Av=amp*gain*v/100 (100 100=circle)\r\n"
    "rot_dir cw|ccw               rotate: sign of sin pair; revolve: omega -> -omega\r\n"
    "corner Q0|Q1|Q2|Q3|run       park revolve at a cardinal, or resume stepping\r\n"
    "contour on|off               enable M1..M4 together (DC contour)\r\n"
    "program P1|P2|stop           preset patterns\r\n"
    "json                         machine-readable status (web UI)\r\n"
    "teleplot on|off              stream PWM B + measured Bx_m/By_m/Bz_m/Babs_m on UART0 (~50 Hz)\r\n"
    "mlx                          show 4 behind-coil probes + reconstructed centre B\r\n"
    "mlx on|off                   poll MLX90393 chips on I2C1\r\n"
    "mlx scan                     rescan I2C1 0x0C..0x1B, bind by address order\r\n"
    "mlx bind <a1> <a2> <a3> <a4> 7-bit addrs for M1..M4 (0=auto)\r\n"
    "mlx map [M#] <X> <Y> <Z>     chip axes -> sensor x up / y left / z axial\r\n"
    "mlx zsign <M#|all> <+|->     chip +z along (+) or against (-) coil +duty field\r\n"
    "mlx k <M#|all> <scale>       reconstruction scale (default 1)\r\n"
    "mlx offset                   coils off → Earth/bias B0 per chip\r\n"
    "fb                           MLX feedback status (per-coil gain/phase, circle err)\r\n"
    "fb on|off                    correct PWM so the MLX centre B matches the commanded circle\r\n"
    "fb reset                     forget the learned correction (K = 1)\r\n"
    "fb rate <0.05..1>            learning step per revolution (default 0.5)\r\n"
    "ping                         heartbeat (replies PONG)\r\n"
    "\r\n"
    "Open-loop duty 0–255. No current sense. DualSense pairs over Bluetooth Classic.\r\n"
    "off all cuts immediately. DC writes to M1..M4 auto-stop the AC field.\r\n"
    "PWM is PCA9685 on I2C0 GPIO21/22 (addr 0x40, ~1 kHz). EN is ESP32 GPIO.\r\n"
    "4x MLX90393 behind M1-M4 on I2C1 GPIO18/19 (3.3 V). Do not share I2C0.\r\n"
    "MLX is display-only unless 'fb on'. Run 'mlx offset' and check zsign first.\r\n"
    "\r\n"
  );
}

static void CLI_PrintStatus(void)
{
  int i;
  uart_printf("mode=open  contour=%u  field=%u  program=%s  axis=%s  freq=%.2f Hz  amp=%u  amp_live=%u  rot_dir=%s\r\n",
              (unsigned)g_contour_active,
              (unsigned)g_rotate_active,
              field_program_str(g_field_prog_want),
              field_axis_str(g_field_prog_want),
              (double)g_rotate_freq_hz,
              (unsigned)g_rotate_amp,
              (unsigned)g_rotate_amp_live,
              rotate_dir_str(g_rotate_dir_want));
  uart_printf("rev_hv h=%u%% v=%u%%  rev_gain=%.4f  (|B|=amp*gain*h|v/100; unequal = ellipse)\r\n",
              (unsigned)g_revolve_h_pct, (unsigned)g_revolve_v_pct,
              (double)Revolve_PreviewGain());
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
  uart_printf("pwm=%u Hz pca9685  sine=%u Hz  coils=%d  pca=%s\r\n",
              (unsigned)PWM_TARGET_HZ, (unsigned)SINE_UPDATE_HZ, NUM_COILS,
              g_pca_ok ? "ok" : "MISSING");
  uart_printf("pairs M1/M3 signed=%d/%d  M2/M4 signed=%d/%d\r\n",
              (int)g_signed_d[0], (int)g_signed_d[2],
              (int)g_signed_d[1], (int)g_signed_d[3]);
  Mlx_PrintStatus();
  Fb_PrintStatus();
  for (i = 0; i < NUM_COILS; i++) {
    CoilState *s = &coils[i];
    uart_printf("M%d en=%u mag=%3u dir=%s duty=%3u\r\n",
                i + 1, s->enabled, s->magnitude,
                s->dir == DIR_FWD ? "+" : "-",
                s->duty_applied);
  }
}

static void CLI_HandleLine(char *line)
{
  char *argv[16];
  int argc = 0;
  const char *cmd;
  char *c;
  for (char *p = strtok(line, " \t\r\n"); p && argc < 16; p = strtok(NULL, " \t\r\n")) {
    argv[argc++] = p;
  }
  if (argc == 0) return;

  for (c = argv[0]; *c; c++) *c = (char)tolower((unsigned char)*c);
  cmd = argv[0];

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    CLI_PrintHelp();
    return;
  }
  if (!strcmp(cmd, "ping")) {
    uart_write("PONG\r\n");
    return;
  }
  if (!strcmp(cmd, "json")) {
    static char js[4096];
    uint8_t pr[NUM_COILS], pl[NUM_COILS];
    int16_t sd[NUM_COILS];
    uint8_t pca, dirty;
    int n;
    int i;
    field_irq_lock();
    memcpy(pr, g_pwm_r, sizeof(pr));
    memcpy(pl, g_pwm_l, sizeof(pl));
    memcpy(sd, (const void *)g_signed_d, sizeof(sd));
    pca = g_pca_ok;
    dirty = g_pca_dirty;
    field_irq_unlock();
    n = snprintf(js, sizeof(js),
                 "{\"ok\":1,\"mode\":\"open\",\"contour\":%u,\"rotate\":%u,"
                 "\"program\":\"%s\",\"program_live\":\"%s\","
                 "\"axis\":\"%s\",\"axis_live\":\"%s\","
                 "\"freq\":%.2f,\"amp\":%u,\"amp_live\":%u,"
                 "\"rev_h\":%u,\"rev_v\":%u,"
                 "\"gamma\":%.2f,\"gamma_live\":%.2f,\"tilt_scale\":%.4f,"
                 "\"rev_gain\":%.4f,"
                 "\"rot_dir\":\"%s\",\"rot_dir_live\":\"%s\",\"n\":%d,"
                 "\"corner\":%d,\"park\":%u,\"travel\":%u,"
                 "\"signed\":[%d,%d,%d,%d],"
                 "\"pr\":[%u,%u,%u,%u],\"pl\":[%u,%u,%u,%u],\"coils\":[",
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
                 (double)Revolve_PreviewGain(),
                 rotate_dir_str(g_rotate_dir_want),
                 rotate_dir_str(g_rotate_dir),
                 NUM_COILS,
                 g_revolve_park ? (int)g_revolve_corner : -1,
                 (unsigned)g_revolve_park,
                 (unsigned)g_revolve_travel,
                 (int)sd[0], (int)sd[1], (int)sd[2], (int)sd[3],
                 (unsigned)pr[0], (unsigned)pr[1], (unsigned)pr[2], (unsigned)pr[3],
                 (unsigned)pl[0], (unsigned)pl[1], (unsigned)pl[2], (unsigned)pl[3]);
    for (i = 0; i < NUM_COILS && n > 0 && n < (int)sizeof(js) - 80; i++) {
      CoilState *s = &coils[i];
      n += snprintf(js + n, sizeof(js) - (size_t)n,
                    "%s{\"id\":%d,\"en\":%u,\"mag\":%u,\"dir\":%d,\"duty\":%u}",
                    i ? "," : "",
                    i + 1, s->enabled, s->magnitude,
                    s->dir == DIR_FWD ? 1 : -1,
                    s->duty_applied);
    }
    if (n > 0 && n < (int)sizeof(js) - 700) {
      n += snprintf(js + n, sizeof(js) - (size_t)n,
                    "],\"mlx\":{\"ok\":%u,\"n\":%u,\"sat\":%u,"
                    "\"bx\":%.3f,\"by\":%.3f,\"bz\":%.3f,\"babs\":%.3f,"
                    "\"center\":{\"bx\":%.3f,\"by\":%.3f,\"bz\":%.3f,\"babs\":%.3f},"
                    "\"coils\":[",
                    (unsigned)(g_mlx_n_present > 0), (unsigned)g_mlx_n_present,
                    (unsigned)g_mlx_center_sat,
                    (double)g_mlx_center_mt[0], (double)g_mlx_center_mt[1],
                    (double)g_mlx_center_mt[2], (double)g_mlx_center_babs,
                    (double)g_mlx_center_mt[0], (double)g_mlx_center_mt[1],
                    (double)g_mlx_center_mt[2], (double)g_mlx_center_babs);
      for (i = 0; i < NUM_COILS && n > 0 && n < (int)sizeof(js) - 180; i++) {
        n += snprintf(js + n, sizeof(js) - (size_t)n,
                      "%s{\"ok\":%u,\"addr\":%u,\"sat\":%u,\"k\":%.3f,"
                      "\"bx\":%.3f,\"by\":%.3f,\"bz\":%.3f,\"babs\":%.3f,"
                      "\"zs\":%d,\"sx\":%.3f,\"sy\":%.3f,\"sz\":%.3f}",
                      i ? "," : "",
                      (unsigned)g_mlx_coil[i].present,
                      (unsigned)g_mlx_coil[i].addr,
                      (unsigned)g_mlx_sat[i],
                      (double)g_mlx_k[i],
                      (double)g_mlx_lab_mt[i][0], (double)g_mlx_lab_mt[i][1],
                      (double)g_mlx_lab_mt[i][2], (double)g_mlx_babs_mt[i],
                      (int)g_mlx_zsign[i],
                      (double)g_mlx_s_mt[i][0], (double)g_mlx_s_mt[i][1],
                      (double)g_mlx_s_mt[i][2]);
      }
      n += snprintf(js + n, sizeof(js) - (size_t)n,
                    "]},\"fb\":{\"en\":%u,\"ok\":%u,\"err\":%.2f,\"updates\":%lu,"
                    "\"fault\":%u,\"gain\":%.5f,\"k\":[%.3f,%.3f,%.3f,%.3f],"
                    "\"ph\":[%.1f,%.1f,%.1f,%.1f]},"
                    "\"pca\":%u,\"dirty\":%u}\r\n",
                    (unsigned)g_fb_en, (unsigned)g_fb.ok,
                    (double)(100.0f * g_fb.err), (unsigned long)g_fb.updates,
                    (unsigned)g_fb.fault, (double)fb_cabs(g_fb.gfit),
                    (double)fb_cabs(g_fb.k[0]), (double)fb_cabs(g_fb.k[1]),
                    (double)fb_cabs(g_fb.k[2]), (double)fb_cabs(g_fb.k[3]),
                    (double)(fb_carg(g_fb.k[0]) * 57.29578f),
                    (double)(fb_carg(g_fb.k[1]) * 57.29578f),
                    (double)(fb_carg(g_fb.k[2]) * 57.29578f),
                    (double)(fb_carg(g_fb.k[3]) * 57.29578f),
                    (unsigned)pca, (unsigned)dirty);
      uart_write(js);
    } else {
      uart_write("{\"ok\":0,\"err\":\"overflow\"}\r\n");
    }
    return;
  }
  if (!strcmp(cmd, "status")) {
    CLI_PrintStatus();
    return;
  }
  if (!strcmp(cmd, "off")) {
    if (argc < 2) { uart_write("usage: off <M#|all>...\r\n"); return; }
    {
      int a;
      for (a = 1; a < argc; a++) {
        uint8_t idx;
        if (str_ieq(argv[a], "all")) {
          Coil_AllOff();
          uart_write("all off\r\n");
          return;
        }
        if (parse_motor_token(argv[a], &idx) == 0) {
          Coil_SetEnabled(idx, 0);
          uart_printf("M%d off\r\n", idx + 1);
        } else {
          uart_printf("unknown motor '%s'\r\n", argv[a]);
        }
      }
    }
    return;
  }
  if (!strcmp(cmd, "on")) {
    if (argc < 2) { uart_write("usage: on <M#>...\r\n"); return; }
    {
      int a;
      for (a = 1; a < argc; a++) {
        uint8_t idx;
        if (parse_motor_token(argv[a], &idx) == 0) {
          coils[idx].duty_applied = coils[idx].magnitude;
          Coil_SetEnabled(idx, 1);
          uart_printf("M%d on mag=%u dir=%s\r\n", idx + 1, coils[idx].magnitude,
                      coils[idx].dir == DIR_FWD ? "+" : "-");
        } else {
          uart_printf("unknown motor '%s'\r\n", argv[a]);
        }
      }
    }
    return;
  }
  if (!strcmp(cmd, "set")) {
    if (argc < 3) { uart_write("usage: set <M#|all> <0..255>\r\n"); return; }
    {
      int mag = atoi(argv[2]);
      if (mag < 0) mag = 0;
      if (mag > 255) mag = 255;
      if (str_ieq(argv[1], "all")) {
        uint8_t i;
        for (i = 0; i < NUM_COILS; i++) Coil_SetMagnitude(i, (uint8_t)mag);
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
    }
    return;
  }
  if (!strcmp(cmd, "dir")) {
    if (argc < 3) { uart_write("usage: dir <M#|all> <+|->\r\n"); return; }
    {
      CoilDir d = (argv[2][0] == '-' || argv[2][0] == 'r' || argv[2][0] == 'R')
                      ? DIR_REV : DIR_FWD;
      if (str_ieq(argv[1], "all")) {
        uint8_t i;
        for (i = 0; i < NUM_COILS; i++) Coil_SetDir(i, d);
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
    }
    return;
  }
  if (!strcmp(cmd, "drive")) {
    if (argc < 4) { uart_write("usage: drive <M#> <0..255> <+|->\r\n"); return; }
    {
      uint8_t idx;
      int mag;
      CoilDir d;
      if (parse_motor_token(argv[1], &idx) != 0) {
        uart_printf("unknown motor '%s'\r\n", argv[1]);
        return;
      }
      mag = atoi(argv[2]);
      if (mag < 0) mag = 0;
      if (mag > 255) mag = 255;
      d = (argv[3][0] == '-') ? DIR_REV : DIR_FWD;
      Coil_SetMagnitude(idx, (uint8_t)mag);
      Coil_SetDir(idx, d);
      Coil_SetEnabled(idx, 1);
      uart_printf("M%d drive mag=%d dir=%s\r\n", idx + 1, mag, d == DIR_FWD ? "+" : "-");
    }
    return;
  }
  if (!strcmp(cmd, "contour")) {
    if (argc < 2) { uart_write("usage: contour on|off\r\n"); return; }
    Rotate_Stop();
    if (str_ieq(argv[1], "on")) {
      int i;
      g_contour_active = 1;
      for (i = 0; i < 4; i++) {
        coils[i].duty_applied = coils[i].magnitude;
        Coil_SetEnabled((uint8_t)i, 1);
      }
      uart_write("contour ON (M1..M4)\r\n");
    } else if (str_ieq(argv[1], "off")) {
      int i;
      g_contour_active = 0;
      for (i = 0; i < 4; i++) Coil_SetEnabled((uint8_t)i, 0);
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
      int i;
      g_contour_active = 1;
      for (i = 0; i < 4; i++) {
        coils[i].duty_applied = coils[i].magnitude;
        Coil_SetEnabled((uint8_t)i, 1);
      }
      uart_write("program P1: M1..M4 ON\r\n");
      return;
    }
    if (str_ieq(argv[1], "P2") || str_ieq(argv[1], "p2")) {
      int i;
      g_contour_active = 1;
      for (i = 0; i < 4; i++) {
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
    } else if (str_ieq(argv[1], "off")) {
      Rotate_RequestStop();
      uart_write("rotate OFF (ramping)\r\n");
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
      return;
    }
    {
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
    }
    return;
  }
  if (!strcmp(cmd, "freq")) {
    float f;
    if (argc < 2) { uart_write("usage: freq <Hz>\r\n"); return; }
    f = (float)atof(argv[1]);
    if (f < ROTATE_FREQ_MIN_HZ) f = ROTATE_FREQ_MIN_HZ;
    if (f > ROTATE_FREQ_MAX_HZ) f = ROTATE_FREQ_MAX_HZ;
    g_rotate_freq_hz = f;
    Rotate_RecalcPhaseInc();
    uart_printf("freq=%.2f Hz\r\n", (double)g_rotate_freq_hz);
    return;
  }
  if (!strcmp(cmd, "amp")) {
    int a, i;
    if (argc < 2) { uart_write("usage: amp <0..255>\r\n"); return; }
    a = atoi(argv[1]);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    g_rotate_amp = (uint8_t)a;
    for (i = 0; i < NUM_ROTATE_COILS && i < NUM_COILS; i++) {
      coils[i].magnitude = g_rotate_amp;
    }
    uart_printf("amp=%u\r\n", (unsigned)g_rotate_amp);
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
      uart_printf("gamma=%.2f deg (live=%.2f)  range 0..180 from +X toward +Z\r\n",
                  (double)g_tilt_gamma_want, (double)g_tilt_gamma_live);
      return;
    }
    if (!field_tilt_parse_gamma(argv[1], &g)) {
      uart_write("usage: gamma <0..180>   (degrees from +X toward +Z; no wrap)\r\n");
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
      if (g_mlx_n_present == 0) mlx_try_init();
      uart_printf("mlx=on present=%u/4\r\n", (unsigned)g_mlx_n_present);
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
    if (str_ieq(argv[1], "bind")) {
      int i;
      if (argc < 6) {
        uart_write("usage: mlx bind <M1addr> <M2addr> <M3addr> <M4addr>  (0=auto)\r\n");
        return;
      }
      for (i = 0; i < NUM_COILS; i++) {
        unsigned v = (unsigned)strtoul(argv[2 + i], NULL, 0);
        if (v > 0x7Fu) {
          uart_write("mlx bind: 7-bit addresses only\r\n");
          return;
        }
        g_mlx_bind[i] = (uint8_t)v;
      }
      mlx_try_init();
      uart_printf("mlx bind 0x%02X 0x%02X 0x%02X 0x%02X  present=%u/4\r\n",
                  (unsigned)g_mlx_bind[0], (unsigned)g_mlx_bind[1],
                  (unsigned)g_mlx_bind[2], (unsigned)g_mlx_bind[3],
                  (unsigned)g_mlx_n_present);
      return;
    }
    if (str_ieq(argv[1], "k")) {
      uint8_t idx = 0;
      int all = 0;
      float k;
      int i;
      if (argc < 4) {
        uart_write("usage: mlx k <M1..M4|all> <scale>\r\n");
        return;
      }
      if (str_ieq(argv[2], "all")) all = 1;
      else if (parse_motor_token(argv[2], &idx) != 0) {
        uart_write("usage: mlx k <M1..M4|all> <scale>\r\n");
        return;
      }
      k = (float)atof(argv[3]);
      if (!(k > 0.0f) || k > 1.0e6f) {
        uart_write("mlx k: scale must be > 0\r\n");
        return;
      }
      if (all) {
        for (i = 0; i < NUM_COILS; i++) g_mlx_k[i] = k;
      } else {
        g_mlx_k[idx] = k;
      }
      Mlx_ReconstructCenter();
      uart_printf("mlx k M%d=%.3f%s\r\n", all ? 0 : (int)(idx + 1), (double)k,
                  all ? " (all)" : "");
      return;
    }
    if (str_ieq(argv[1], "zsign")) {
      uint8_t idx = 0;
      int all = 0;
      int8_t zs;
      int i;
      if (argc < 4) {
        uart_write("usage: mlx zsign <M1..M4|all> <+|->\r\n");
        return;
      }
      if (str_ieq(argv[2], "all")) all = 1;
      else if (parse_motor_token(argv[2], &idx) != 0) {
        uart_write("usage: mlx zsign <M1..M4|all> <+|->\r\n");
        return;
      }
      if (!strcmp(argv[3], "+") || !strcmp(argv[3], "+1") || !strcmp(argv[3], "1")) {
        zs = 1;
      } else if (!strcmp(argv[3], "-") || !strcmp(argv[3], "-1")) {
        zs = -1;
      } else {
        uart_write("mlx zsign: need + or -\r\n");
        return;
      }
      if (all) {
        for (i = 0; i < NUM_COILS; i++) g_mlx_zsign[i] = zs;
      } else {
        g_mlx_zsign[idx] = zs;
      }
      Mlx_ReconstructCenter();
      uart_printf("mlx zsign M%d=%c%s\r\n", all ? 0 : (int)(idx + 1),
                  zs < 0 ? '-' : '+', all ? " (all)" : "");
      return;
    }
    if (str_ieq(argv[1], "map")) {
      uint8_t src[3];
      int8_t sgn[3];
      uint8_t used = 0;
      int i;
      int arg0 = 2;
      int coil = -1;
      uint8_t idx;
      if (argc >= 6 && parse_motor_token(argv[2], &idx) == 0) {
        coil = (int)idx;
        arg0 = 3;
      }
      if (argc < arg0 + 3) {
        uart_write("usage: mlx map [M#] <labX> <labY> <labZ>   e.g. mlx map x y z\r\n");
        return;
      }
      for (i = 0; i < 3; i++) {
        if (!mlx_parse_axis_tok(argv[arg0 + i], &src[i], &sgn[i])) {
          uart_write("mlx map: need x|y|z with optional + or - prefix\r\n");
          return;
        }
        if (used & (uint8_t)(1u << src[i])) {
          uart_write("mlx map: each chip axis once\r\n");
          return;
        }
        used |= (uint8_t)(1u << src[i]);
      }
      if (coil >= 0) {
        for (i = 0; i < 3; i++) {
          g_mlx_src[coil][i] = src[i];
          g_mlx_sgn[coil][i] = sgn[i];
        }
      } else {
        int cc;
        for (cc = 0; cc < NUM_COILS; cc++) {
          for (i = 0; i < 3; i++) {
            g_mlx_src[cc][i] = src[i];
            g_mlx_sgn[cc][i] = sgn[i];
          }
        }
      }
      if (coil >= 0) {
        uart_printf("mlx map M%d labXYZ=%c%c %c%c %c%c\r\n", coil + 1,
                    sgn[0] < 0 ? '-' : '+', "xyz"[src[0]],
                    sgn[1] < 0 ? '-' : '+', "xyz"[src[1]],
                    sgn[2] < 0 ? '-' : '+', "xyz"[src[2]]);
      } else {
        uart_printf("mlx map all labXYZ=%c%c %c%c %c%c\r\n",
                    sgn[0] < 0 ? '-' : '+', "xyz"[src[0]],
                    sgn[1] < 0 ? '-' : '+', "xyz"[src[1]],
                    sgn[2] < 0 ? '-' : '+', "xyz"[src[2]]);
      }
      return;
    }
    if (str_ieq(argv[1], "offset")) {
      if (g_mlx_n_present == 0) { uart_write("mlx missing\r\n"); return; }
      if (g_rotate_active) Rotate_Stop();
      if (mlx_take_offset()) {
        uart_write("mlx offset set (per coil)\r\n");
        Mlx_PrintStatus();
      } else {
        uart_write("mlx offset failed\r\n");
      }
      return;
    }
    uart_write("usage: mlx | on|off | scan | bind | map | zsign | k | offset\r\n");
    return;
  }
  if (!strcmp(cmd, "fb") || !strcmp(cmd, "feedback")) {
    if (argc < 2) {
      Fb_PrintStatus();
      return;
    }
    if (str_ieq(argv[1], "on") || str_ieq(argv[1], "1")) {
      if (!Fb_SetEnabled(1)) {
        uart_printf("fb on: needs all 4 MLX chips (present=%u/4); try 'mlx scan'\r\n",
                    (unsigned)g_mlx_n_present);
        return;
      }
      if (!g_mlx_en) uart_write("fb on: note 'mlx off' freezes the correction\r\n");
      uart_write("fb=on (learning starts once the field is steady)\r\n");
      return;
    }
    if (str_ieq(argv[1], "off") || str_ieq(argv[1], "0")) {
      (void)Fb_SetEnabled(0);
      uart_write("fb=off (open-loop PWM; still estimating err)\r\n");
      return;
    }
    if (str_ieq(argv[1], "reset")) {
      Fb_Reset();
      uart_write("fb reset (K = 1)\r\n");
      return;
    }
    if (str_ieq(argv[1], "rate")) {
      float mu = (argc >= 3) ? (float)atof(argv[2]) : 0.0f;
      if (!(mu >= 0.05f && mu <= 1.0f)) {
        uart_write("usage: fb rate <0.05..1>\r\n");
        return;
      }
      g_fb_cfg.mu = mu;
      uart_printf("fb rate=%.2f\r\n", (double)mu);
      return;
    }
    uart_write("usage: fb | on|off | reset | rate <0.05..1>\r\n");
    return;
  }
  if (!strcmp(cmd, "teleplot") || !strcmp(cmd, "tp")) {
    if (argc < 2) { uart_write("usage: teleplot on|off\r\n"); return; }
    if (str_ieq(argv[1], "on") || str_ieq(argv[1], "1")) {
      g_teleplot_en = 1;
      uart_write("teleplot=on (UART0 ~50 Hz; open Teleplot on Serial Monitor)\r\n");
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
      uart_write("axis=x program=revolve (YZ, 4-coil gamma=0)\r\n");
    } else if (str_ieq(argv[1], "z") || str_ieq(argv[1], "xy")) {
      Field_SetProgram(FIELD_REVOLVE_Z);
      uart_write("axis=z program=revolve (XY, 4-coil gamma=90)\r\n");
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

void CLI_Exec(const char *line, char *reply, size_t reply_cap)
{
  char tmp[CLI_LINE_MAX];
  size_t n;
  if (!line) return;
  n = strlen(line);
  if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
  memcpy(tmp, line, n);
  tmp[n] = '\0';
  g_reply_buf = reply;
  g_reply_cap = reply_cap;
  g_reply_len = 0;
  g_reply_to_serial = (reply == NULL) ? 1 : 0;
  if (reply && reply_cap) reply[0] = '\0';
  CLI_HandleLine(tmp);
  g_reply_buf = NULL;
  g_reply_cap = 0;
  g_reply_to_serial = 1;
}

void CLI_PollSerial(void)
{
  int ch;
  while ((ch = serial_read_byte()) >= 0) {
    if (ch == '\r' || ch == '\n') {
      if (g_uart_len > 0) {
        g_uart_line[g_uart_len] = '\0';
        if (g_uart_echo) serial_write_bytes("\r\n");
        CLI_Exec(g_uart_line, NULL, 0);
        if (g_uart_echo) serial_write_bytes("> ");
        g_uart_len = 0;
      } else if (g_uart_echo) {
        serial_write_bytes("\r\n> ");
      }
    } else if (ch == 0x08 || ch == 0x7F) {
      if (g_uart_len > 0) {
        g_uart_len--;
        if (g_uart_echo) serial_write_bytes("\b \b");
      }
    } else if (g_uart_len + 1 < CLI_LINE_MAX && ch >= 32 && ch < 127) {
      g_uart_line[g_uart_len++] = (char)ch;
      if (g_uart_echo) {
        char one[2] = { (char)ch, 0 };
        serial_write_bytes(one);
      }
    }
  }
}
