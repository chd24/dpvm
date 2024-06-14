/* dpvm: init; T15.406-T18.508; $DVS:time$ */

#include <string.h>
#include <signal.h>
#include "bytecode.h"
#include "cache.h"
#include "config.h"
#include "hash.h"
#include "init.h"
#include "io.h"
#include "name.h"
#include "object.h"
#include "run.h"
#include "task.h"
#include "thread.h"

int dpvm_sigint = 0;

static void dpvm_sigint_handler(int sig) {
	dpvm_sigint = 1;
}

int64_t dpvm_init(struct dpvm *dpvm, int flags) {
        int64_t err;

	signal(SIGINT, dpvm_sigint_handler);

	memset(dpvm, 0, sizeof(struct dpvm));

	dpvm->flags = flags;

	if ((err = dpvm_hash_global_init()))
                return (-err << 4 | 1) << 8;

	if ((err = dpvm_object_init(dpvm)))
                return (-err << 4 | 2) << 8;

	if ((err = dpvm_bytecode_init(dpvm)))
                return (-err << 4 | 3) << 8;

	if ((err = dpvm_thread_init(dpvm)))
                return (-err << 4 | 4) << 8;
\
	if ((err = dpvm_task_init(dpvm)))
                return (-err << 4 | 5) << 8;

	if ((err = dpvm_io_init(dpvm)))
                return (-err << 4 | 6) << 8;

	if ((err = dpvm_cache_init(dpvm)))
		return (err << 4 | 7) << 8;

	if ((err = dpvm_thread_post_init(dpvm)))
                return (-err << 4 | 8) << 8;

	if ((err = dpvm_name_init(dpvm)))
                return (-err << 4 | 9) << 8;

	if ((err = dpvm_config_init(dpvm)))
                return (-err << 4 | 10) << 8;

        dpvm->inited = -1;

        if ((err = dpvm_run_init(dpvm)))
                return err;

	dpvm->inited = 1;

	return 0;
}

int dpvm_finish(struct dpvm *dpvm) {
	dpvm->inited = 0;
	dpvm_cache_finish(dpvm);
	return 0;
}
