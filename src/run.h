/* dpvm: bytecode; T16.049-T19.605; $DVS:time$ */

#ifndef DPVM_RUN
#define DPVM_RUN

#include "init.h"

extern int64_t dpvm_fsplit(int64_t ieee, int64_t *pexponent);
extern int64_t dpvm_fmerge(int64_t mantissa, int64_t exponent);

extern int64_t dpvm_run_init(struct dpvm *dpvm);
extern int dpvm_get_last_code(struct dpvm_object *thread);
extern int64_t dpvm_check_function(struct dpvm_object *parent, struct dpvm_object *func, int flags);
extern int64_t dpvm_run_function(struct dpvm_object *thread);
extern int64_t dpvm_run_thread(struct dpvm_object *thread, int64_t nsteps);

#endif
