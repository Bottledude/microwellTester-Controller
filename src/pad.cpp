#include "pad.h"
#include "cmd_queue.h"

#include <Arduino.h>
#include <Bluepad32.h>
#include "controller/uni_gamepad.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static ControllerPtr s_pad = nullptr;
static uint8_t s_prev_dpad;
static uint8_t s_prev_a, s_prev_b, s_prev_x, s_prev_y;
static uint8_t s_prev_l1, s_prev_r1;
static uint8_t s_prev_start, s_prev_select, s_prev_sys, s_prev_cap;
static const char *s_axis_prog = "y";
static float s_gamma = 45.0f;
static int s_amp = -1;
static float s_freq = 1.0f;
static float s_freq_sent = -1.0f;
static uint32_t s_last_ms;

static const float DEAD = 0.25f;
static const float GAMMA_STEP = 5.0f;
static const int AMP_STEP = 5;
static const float FREQ_MIN = 0.01f;
static const float FREQ_MAX = 50.0f;
static const float FREQ_SLEW = 4.0f;
static const float AXIS_SCALE = 512.0f;

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

void Pad_Begin(void)
{
  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.enableVirtualDevice(false);
  s_last_ms = millis();
  Serial.println("Bluepad32: pair DualSense over Bluetooth Classic");
}

void Pad_Poll(void)
{
  BP32.update();
  if (!s_pad || !s_pad->isConnected()) return;

  uint32_t now = millis();
  float dt = (now - s_last_ms) / 1000.0f;
  s_last_ms = now;
  if (dt <= 0.0f) dt = 0.02f;
  if (dt > 0.2f) dt = 0.2f;

  uint8_t dpad = s_pad->dpad();
  if (dpad != s_prev_dpad) {
    if (dpad & DPAD_UP) {
      s_axis_prog = "y";
      CmdQ_Enqueue("axis y", 0);
    } else if (dpad & DPAD_LEFT) {
      s_axis_prog = "x";
      CmdQ_Enqueue("axis x", 0);
    } else if (dpad & DPAD_RIGHT) {
      s_axis_prog = "z";
      CmdQ_Enqueue("axis z", 0);
    } else if (dpad & DPAD_DOWN) {
      s_axis_prog = "tilt";
      CmdQ_Enqueue("axis tilt", 0);
    }
    s_prev_dpad = dpad;
  }

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
  if (edge(s_pad->x() ? 1 : 0, &s_prev_x)) CmdQ_Enqueue("corner Q0", 0);
  if (edge(s_pad->a() ? 1 : 0, &s_prev_a)) CmdQ_Enqueue("corner Q1", 0);
  if (edge(s_pad->b() ? 1 : 0, &s_prev_b)) CmdQ_Enqueue("corner Q2", 0);
  if (edge(s_pad->y() ? 1 : 0, &s_prev_y)) CmdQ_Enqueue("corner Q3", 0);
  if (edge(s_pad->miscCapture() ? 1 : 0, &s_prev_cap)) CmdQ_Enqueue("status", 0);

  float lx = axis_n(s_pad->axisX());
  if (lx > DEAD || lx < -DEAD) {
    float g = (lx + 1.0f) * 90.0f;
    g = roundf(g / GAMMA_STEP) * GAMMA_STEP;
    if (g < 0.0f) g = 0.0f;
    if (g > 180.0f) g = 180.0f;
    if (fabsf(g - s_gamma) >= (GAMMA_STEP - 0.01f)) {
      char cmd[24];
      s_gamma = g;
      snprintf(cmd, sizeof(cmd), "gamma %.0f", (double)g);
      CmdQ_Enqueue(cmd, 0);
    }
  }

  float rx = axis_n(s_pad->axisRX());
  if (rx > DEAD || rx < -DEAD) {
    int a = (int)lroundf((rx + 1.0f) * 0.5f * 255.0f / (float)AMP_STEP) * AMP_STEP;
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    if (s_amp < 0 || a != s_amp) {
      char cmd[16];
      s_amp = a;
      snprintf(cmd, sizeof(cmd), "amp %d", a);
      CmdQ_Enqueue(cmd, 0);
    }
  }

  float ry = axis_n(s_pad->axisRY());
  if (ry > DEAD || ry < -DEAD) {
    s_freq += (-ry) * FREQ_SLEW * dt;
    if (s_freq < FREQ_MIN) s_freq = FREQ_MIN;
    if (s_freq > FREQ_MAX) s_freq = FREQ_MAX;
    float q = roundf(s_freq * 4.0f) / 4.0f;
    if (q < FREQ_MIN) q = FREQ_MIN;
    if (q != s_freq_sent) {
      char cmd[24];
      s_freq_sent = q;
      snprintf(cmd, sizeof(cmd), "freq %.2f", (double)q);
      CmdQ_Enqueue(cmd, 0);
    }
  }
}
