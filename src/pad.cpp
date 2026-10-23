#include "pad.h"
#include "cmd_queue.h"
#include "field_engine.h"

#include <Arduino.h>
#include <Bluepad32.h>
#include "controller/uni_gamepad.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static ControllerPtr s_pad = nullptr;
static uint8_t s_prev_a, s_prev_b, s_prev_x, s_prev_y;
static uint8_t s_prev_l1, s_prev_r1, s_prev_l3;
static uint8_t s_prev_start, s_prev_select, s_prev_sys, s_prev_cap;
static const char *s_axis_prog = "y";
static int s_amp_sh;
static float s_gamma_sh;
static float s_freq_sh;
static uint8_t s_adj_kind; /* 0 none, 1 amp, 2 gamma, 3 freq */
static int s_adj_dir;
static uint32_t s_adj_next_ms;
static uint8_t s_aiming;
static int s_aim_q;

static const float DEAD = 0.25f;
static const float GAMMA_STEP = 5.0f;
static const int AMP_STEP = 5;
static const float FREQ_MIN = 0.01f;
static const float FREQ_MAX = 50.0f;
static const float AXIS_SCALE = 512.0f;
static const uint32_t ADJ_DELAY_MS = 350;
static const uint32_t ADJ_REPEAT_MS = 120;
static const int AIM_STEP_DEG = 2;

static void onConnectedController(ControllerPtr ctl)
{
  if (!s_pad) {
    s_pad = ctl;
    Serial.println("Pad connected");
  }
}

static void onDisconnectedController(ControllerPtr ctl)
{
  if (s_pad == ctl) {
    s_pad = nullptr;
    if (s_aiming) {
      s_aiming = 0;
      CmdQ_Enqueue("aim run", 0);
    }
    Serial.println("Pad disconnected");
  }
}

static uint8_t edge(uint8_t cur, uint8_t *prev)
{
  uint8_t e = (cur && !*prev) ? 1 : 0;
  *prev = cur;
  return e;
}

static float axis_n(int32_t raw)
{
  float v = (float)raw / AXIS_SCALE;
  if (v > 1.0f) v = 1.0f;
  if (v < -1.0f) v = -1.0f;
  return v;
}

static void select_axis(const char *axis)
{
  char cmd[24];
  s_axis_prog = axis;
  snprintf(cmd, sizeof(cmd), "axis %s", axis);
  CmdQ_Enqueue(cmd, 0);
}

static float freq_stepped(float cur, int dir)
{
  float step;
  if (cur < 0.1f) step = 0.01f;
  else if (cur < 1.0f) step = 0.05f;
  else if (cur < 10.0f) step = 0.25f;
  else step = 1.0f;
  cur += (float)dir * step;
  if (cur < FREQ_MIN) cur = FREQ_MIN;
  if (cur > FREQ_MAX) cur = FREQ_MAX;
  return roundf(cur * 100.0f) / 100.0f;
}

static void adj_step(int kind, int dir)
{
  char cmd[32];
  if (kind == 1) {
    int a = s_amp_sh + dir * AMP_STEP;
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    if (a == s_amp_sh) return;
    s_amp_sh = a;
    snprintf(cmd, sizeof(cmd), "amp %d", a);
    CmdQ_Enqueue(cmd, 0);
  } else if (kind == 2) {
    float g = s_gamma_sh + (float)dir * GAMMA_STEP;
    if (g < 0.0f) g = 0.0f;
    if (g > 180.0f) g = 180.0f;
    g = roundf(g);
    if (g == s_gamma_sh) return;
    s_gamma_sh = g;
    snprintf(cmd, sizeof(cmd), "gamma %.0f", (double)g);
    CmdQ_Enqueue(cmd, 0);
  } else if (kind == 3) {
    float f = freq_stepped(s_freq_sh, dir);
    if (f == s_freq_sh) return;
    s_freq_sh = f;
    snprintf(cmd, sizeof(cmd), "freq %.2f", (double)f);
    CmdQ_Enqueue(cmd, 0);
  }
}

static void poll_adjust(uint32_t now, uint8_t dpad, uint8_t l2, uint8_t r2)
{
  int kind = 0;
  int dir = 0;
  if (l2 && r2) kind = 3;
  else if (l2) kind = 1;
  else if (r2) kind = 2;
  if (dpad & DPAD_UP) dir = 1;
  else if (dpad & DPAD_DOWN) dir = -1;

  if (!kind || !dir) {
    s_adj_kind = 0;
    s_adj_dir = 0;
    return;
  }
  if ((uint8_t)kind != s_adj_kind || dir != s_adj_dir) {
    s_adj_kind = (uint8_t)kind;
    s_adj_dir = dir;
    if (kind == 1) s_amp_sh = (int)g_rotate_amp;
    else if (kind == 2) s_gamma_sh = g_tilt_gamma_want;
    else s_freq_sh = g_rotate_freq_hz;
    adj_step(kind, dir);
    s_adj_next_ms = now + ADJ_DELAY_MS;
    return;
  }
  if ((int32_t)(now - s_adj_next_ms) >= 0) {
    adj_step(kind, dir);
    s_adj_next_ms = now + ADJ_REPEAT_MS;
  }
}

static void poll_aim(float rx, float ry)
{
  float mag = sqrtf(rx * rx + ry * ry);
  char cmd[24];
  if (mag >= DEAD) {
    float deg = atan2f(-ry, rx) * (180.0f / 3.14159265f);
    int q, d;
    if (deg < 0.0f) deg += 360.0f;
    q = (int)lroundf(deg);
    if (q >= 360) q = 0;
    if (q < 0) q = 0;
    d = q - s_aim_q;
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    if (!s_aiming || d <= -AIM_STEP_DEG || d >= AIM_STEP_DEG) {
      s_aiming = 1;
      s_aim_q = q;
      snprintf(cmd, sizeof(cmd), "aim %d", q);
      CmdQ_Enqueue(cmd, 0);
    }
  } else if (s_aiming) {
    s_aiming = 0;
    CmdQ_Enqueue("aim run", 0);
  }
}

void Pad_Begin(void)
{
  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.enableVirtualDevice(false);
  Serial.println("Bluepad32: pair DualSense over Bluetooth Classic");
}

void Pad_Poll(void)
{
  uint32_t now;
  uint8_t dpad;
  uint8_t l2, r2;
  float rx, ry;

  BP32.update();
  if (!s_pad || !s_pad->isConnected()) return;

  now = millis();
  dpad = s_pad->dpad();
  /* Digital click or a real pull. Resting analog is 0. */
  l2 = (s_pad->l2() || s_pad->brake() > 200) ? 1 : 0;
  r2 = (s_pad->r2() || s_pad->throttle() > 200) ? 1 : 0;

  if (edge(s_pad->x() ? 1 : 0, &s_prev_x)) select_axis("y");
  if (edge(s_pad->a() ? 1 : 0, &s_prev_a)) select_axis("x");
  if (edge(s_pad->b() ? 1 : 0, &s_prev_b)) select_axis("z");
  if (edge(s_pad->y() ? 1 : 0, &s_prev_y)) select_axis("tilt");

  poll_adjust(now, dpad, l2, r2);

  if (edge(s_pad->miscStart() ? 1 : 0, &s_prev_start)) {
    char cmd[32];
    if (s_axis_prog[0] == 'y' && s_axis_prog[1] == '\0') {
      CmdQ_Enqueue("rotate on", 0);
    } else if (!strcmp(s_axis_prog, "tilt")) {
      CmdQ_Enqueue("revolve tilt", 0);
    } else {
      snprintf(cmd, sizeof(cmd), "revolve %s", s_axis_prog);
      CmdQ_Enqueue(cmd, 0);
    }
  }
  if (edge(s_pad->miscSelect() ? 1 : 0, &s_prev_select)) {
    CmdQ_Enqueue("rotate off", 0);
  }
  if (edge(s_pad->miscSystem() ? 1 : 0, &s_prev_sys)) {
    CmdQ_Enqueue("off all", 1);
  }
  if (edge(s_pad->l1() ? 1 : 0, &s_prev_l1)) CmdQ_Enqueue("rot_dir ccw", 0);
  if (edge(s_pad->r1() ? 1 : 0, &s_prev_r1)) CmdQ_Enqueue("rot_dir cw", 0);
  if (edge(s_pad->thumbL() ? 1 : 0, &s_prev_l3)) {
    CmdQ_Enqueue(g_fb_apply ? "fb off" : "fb on", 0);
  }
  if (edge(s_pad->miscCapture() ? 1 : 0, &s_prev_cap)) CmdQ_Enqueue("status", 0);

  rx = axis_n(s_pad->axisRX());
  ry = axis_n(s_pad->axisRY());
  poll_aim(rx, ry);
}
