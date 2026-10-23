#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Pad_Begin(void);
void Pad_Poll(void); /* core 0: Bluepad32 update + enqueue CLI */

#ifdef __cplusplus
}
#endif
