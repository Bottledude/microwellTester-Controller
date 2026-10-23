#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t g_teleplot_en;

void uart_write(const char *s);
void uart_printf(const char *fmt, ...);
void CLI_Exec(const char *line, char *reply, size_t reply_cap);
void CLI_PollSerial(void);
void Teleplot_Emit(void);

#ifdef __cplusplus
}
#endif
