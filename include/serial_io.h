#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void serial_write_bytes(const char *s);
int serial_read_byte(void); /* -1 if none */

#ifdef __cplusplus
}
#endif
