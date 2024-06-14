/* dpvm: bytecode; T15.395-T18.229; $DVS:time$ */

#ifndef DPVM_BYTECODE
#define DPVM_BYTECODE

#define int enum {
#define _Z 0};
#include "../dpvm/common/bytecodes.dpvmh"
#undef _Z
#undef int
#include "init.h"

extern int dpvm_bytecode_init(struct dpvm *dpvm);
extern int64_t dpvm_bytecode_run(struct dpvm_object *thread, int64_t nsteps);

#endif
