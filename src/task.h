/* dpvm: task; T16.620-T19.698; $DVS:time$ */

#ifndef DPVM_TASK
#define DPVM_TASK

#include "init.h"
#include "io.h"

#define DPVM_TASK_NAME_LEN (DPVM_SYS_PARAM_NAME_END - DPVM_SYS_PARAM_NAME)

enum dpvm_task_ints {
	DPVM_TASK_INT_FLAGS,
	DPVM_TASK_INT_START_TIME,
	DPVM_TASK_INT_ID,
	DPVM_TASK_INT_N_CHILDS,
	DPVM_TASK_INT_MEMORY,
	DPVM_TASK_INT_MEMORY_QUOTA,
	DPVM_TASK_INT_CHECKER_HASH,
	DPVM_TASK_INT_TRANSLATOR_HASH	= DPVM_TASK_INT_CHECKER_HASH + 4,
	DPVM_TASK_INT_NAME		= DPVM_TASK_INT_TRANSLATOR_HASH + 4,
	DPVM_TASK_INT_NAME_END		= DPVM_TASK_INT_NAME + DPVM_TASK_NAME_LEN,
	DPVM_TASK_INT_END		= DPVM_TASK_INT_NAME_END
};

#define int enum {
#define _Z 0};
#include "../dpvm/common/taskFlags.dpvmh"
#undef _Z
#undef int

enum dpvm_task_links {
	DPVM_TASK_LINK_HANDLER,
	DPVM_TASK_LINK_CHECKER,
	DPVM_TASK_LINK_TRANSLATOR,
	DPVM_TASK_LINK_PARENT,
	DPVM_TASK_LINK_CHILD,
	DPVM_TASK_LINK_NEXT,
	DPVM_TASK_LINK_END
};

extern int dpvm_task_init(struct dpvm *dpvm);
extern int64_t dpvm_task_create(struct dpvm_object *thread);
extern int64_t dpvm_task_getsys(struct dpvm_object *thread, int64_t param);
extern int64_t dpvm_task_setsys(struct dpvm_object *thread, int64_t param, int64_t value);

#endif
