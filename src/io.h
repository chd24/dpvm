/* dpvm: input-output functions; T16.458-T19.050; $DVS:time$ */

#ifndef DPVM_IO
#define DPVM_IO

#include "object.h"
#include "init.h"

#define int enum {
#define _Z 0};
#include "../dpvm/common/sysParams.dpvmh"
#undef _Z
#undef int

struct dpvm_io_run_func { 
	int code; 
	struct dpvm_object *(*run)(struct dpvm_object *); 
};

extern const struct dpvm_io_run_func dpvm_io_run_funcs[];

extern int dpvm_io_init(struct dpvm *dpvm);
extern void dpvm_io_set_input_buf(struct dpvm *dpvm, const char *buf);
extern size_t dpvm_io_get_output_buf(struct dpvm *dpvm, char *buf, size_t buf_size);
extern void dpvm_sleep_until(int64_t t);
extern int64_t dpvm_get_time();

#endif
