#include "cmd_queue.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

#define CMDQ_N 16
#define CMDQ_LINE 128

static char s_q[CMDQ_N][CMDQ_LINE];
static uint8_t s_urgent[CMDQ_N];
static uint8_t s_head, s_tail, s_count;
static SemaphoreHandle_t s_mu;

void CmdQ_Init(void)
{
  s_mu = xSemaphoreCreateMutex();
  s_head = s_tail = s_count = 0;
}

void CmdQ_Enqueue(const char *line, uint8_t urgent)
{
  size_t n;
  if (!line || !s_mu) return;
  n = strlen(line);
  if (n == 0 || n >= CMDQ_LINE) return;

  xSemaphoreTake(s_mu, portMAX_DELAY);
  if (urgent) {
    s_head = s_tail = s_count = 0;
  } else {
    /* newest non-urgent verb wins */
    const char *sp = strchr(line, ' ');
    size_t vlen = sp ? (size_t)(sp - line) : n;
    uint8_t i = s_head;
    uint8_t c = s_count;
    uint8_t w = 0;
    char compact[CMDQ_N][CMDQ_LINE];
    uint8_t compact_u[CMDQ_N];
    uint8_t k;
    for (k = 0; k < c; k++) {
      uint8_t keep = 1;
      if (!s_urgent[i]) {
        size_t oldv = strcspn(s_q[i], " ");
        if (oldv == vlen && strncmp(s_q[i], line, vlen) == 0) keep = 0;
      }
      if (keep) {
        memcpy(compact[w], s_q[i], CMDQ_LINE);
        compact_u[w] = s_urgent[i];
        w++;
      }
      i = (uint8_t)((i + 1u) % CMDQ_N);
    }
    memcpy(s_q, compact, sizeof(compact));
    memcpy(s_urgent, compact_u, sizeof(compact_u));
    s_head = 0;
    s_count = w;
    s_tail = w;
  }
  if (s_count >= CMDQ_N) {
    s_head = (uint8_t)((s_head + 1u) % CMDQ_N);
    s_count--;
  }
  memcpy(s_q[s_tail], line, n + 1);
  s_urgent[s_tail] = urgent ? 1 : 0;
  s_tail = (uint8_t)((s_tail + 1u) % CMDQ_N);
  s_count++;
  xSemaphoreGive(s_mu);
}

uint8_t CmdQ_Dequeue(char *out, size_t cap)
{
  uint8_t ok = 0;
  if (!out || cap == 0 || !s_mu) return 0;
  xSemaphoreTake(s_mu, portMAX_DELAY);
  if (s_count) {
    strncpy(out, s_q[s_head], cap - 1);
    out[cap - 1] = '\0';
    s_head = (uint8_t)((s_head + 1u) % CMDQ_N);
    s_count--;
    ok = 1;
  }
  xSemaphoreGive(s_mu);
  return ok;
}
