/* dpvm: input; T15.422-T18.498; $DVS:time$ */

#ifndef DPVM_INPUT
#define DPVM_INPUT

#include <stdint.h>

extern void dpvm_input_init(void);
extern int dpvm_input_read(volatile int64_t *taskFlags, const char *prompt, char *cmd, size_t maxlen);

#endif
