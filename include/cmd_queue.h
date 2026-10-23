#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void CmdQ_Init(void);
void CmdQ_Enqueue(const char *line, uint8_t urgent);
uint8_t CmdQ_Dequeue(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
