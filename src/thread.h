/* dpvm: thread; T15.530-T19.605; $DVS:time$ */

#ifndef DPVM_THREAD
#define DPVM_THREAD

#include <stdint.h>
#include "init.h"

enum dpvm_thread_int_header {
	DPVM_THREAD_INT_FLAGS,
	DPVM_THREAD_INT_TIME,
	DPVM_THREAD_INT_INPUT,
	DPVM_THREAD_INT_OUTPUT,
	DPVM_THREAD_INT_CHILD,
	DPVM_THREAD_INT_POS,
	DPVM_THREAD_INT_PORT = DPVM_THREAD_INT_POS,
	DPVM_THREAD_INT_SIZE,
	DPVM_THREAD_INT_IPADDR0 = DPVM_THREAD_INT_SIZE,
	DPVM_THREAD_INT_EXT,
        DPVM_THREAD_INT_ERROR = DPVM_THREAD_INT_EXT,
        DPVM_THREAD_INT_IPADDR1 = DPVM_THREAD_INT_EXT,
	DPVM_THREAD_INT_END
};

enum dpvm_thread_int_frame {
	DPVM_THREAD_INT_EXT_STACK_NLINKS,
	DPVM_THREAD_INT_EXT_STACK_NINTS,
	DPVM_THREAD_INT_EXT_STACK_NFLOATS,
	DPVM_THREAD_INT_EXT_STACK_NCODES,
	DPVM_THREAD_INT_EXT_NLINKS,
	DPVM_THREAD_INT_EXT_PC,
	DPVM_THREAD_INT_EXT_END
};

enum dpvm_thread_link_header {
	DPVM_THREAD_LINK_STACK,
	DPVM_THREAD_LINK_RESULT,
	DPVM_THREAD_LINK_PARENT,
	DPVM_THREAD_LINK_TASK,
	DPVM_THREAD_LINK_DATA,
	DPVM_THREAD_LINK_NAME,
	DPVM_THREAD_LINK_ARG,
	DPVM_THREAD_LINK_FUNC,
	DPVM_THREAD_LINK_END
};

enum dpvm_thread_flags {
	DPVM_THREAD_FLAG_FINISH		= 0x0000001,
	DPVM_THREAD_FLAG_IO		= 0x0000002,
	DPVM_THREAD_FLAG_FORCE_CHECK	= 0x0000004,
	DPVM_THREAD_FLAG_MUST_TRANSLATE	= 0x0000008,
        DPVM_THREAD_FLAG_RUN		= 0x0000010,
        DPVM_THREAD_FLAG_ERROR		= 0x0000020,
        DPVM_THREAD_FLAG_NO_PARENT	= 0x0000040, /* in dpvm_thread_create() use parent parameters for copy task only */
        DPVM_THREAD_FLAG_INTERNAL	= 0x0000080, /* internal thread for check or translate function */
        DPVM_THREAD_FLAG_IO_CODE	= 0x0000100,
};

extern int dpvm_thread_init(struct dpvm *dpvm);
extern int dpvm_thread_post_init(struct dpvm *dpvm);
extern struct dpvm_object *dpvm_thread_create(struct dpvm_object *func,
		struct dpvm_object *arg,  struct dpvm_object *name,
		struct dpvm_object *data, struct dpvm_object *parent,
		int flags, int in, int out, size_t pos, uint64_t size, uint64_t ext);
extern void dpvm_thread_unlink(struct dpvm_object *thread);
extern int dpvm_thread_start(struct dpvm_object *thread, struct dpvm_object **links);
extern int dpvm_thread_finish(struct dpvm_object *thread);
extern int dpvm_thread_finished(struct dpvm_object *thread);
extern int dpvm_thread_prepare_result(struct dpvm_object *thread, struct dpvm_object *result_type);
extern int dpvm_thread_get_result(struct dpvm_object **pthread);
extern int dpvm_thread_run(struct dpvm_object *thread, struct dpvm_object **links);
extern int dpvm_thread_io(struct dpvm_object *thread, int code);
extern int dpvm_thread_run_transaction(struct dpvm_object *thread, struct dpvm_object *transaction, struct dpvm_object **links);
extern int dpvm_thread_do_run(struct dpvm_object **links, int64_t *ints,
                              double *floats, uint8_t *codes, struct dpvm_object *thread);
extern int dpvm_thread_do_wait(struct dpvm_object **links, int64_t *ints,
                               double *floats, uint8_t *codes, struct dpvm_object *thread);
extern int64_t dpvm_thread_stats(struct dpvm *dpvm, int64_t n_getsys_param);
extern void dpvm_thread_change_nactive(struct dpvm *dpvm, int diff);
extern struct dpvm_object *dpvm_thread_type_transaction(struct dpvm *dpvm);

#endif
