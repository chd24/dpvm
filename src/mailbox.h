/* dpvm: mailbox; T16.625-T19.630; $DVS:time$ */

#ifndef DPVM_MAILBOX
#define DPVM_MAILBOX

#include "init.h"

extern int dpvm_mailbox_init(struct dpvm *dpvm);
extern int dpvm_mailbox_send(struct dpvm_object *thread, struct dpvm_object *address, struct dpvm_object *message);
extern struct dpvm_object *dpvm_mailbox_receive(struct dpvm_object *thread,
		struct dpvm_object *address, int64_t timeout, int64_t *status);
extern void dpvm_mailbox_clear_task(struct dpvm_object *task);
extern int64_t dpvm_mailbox_stats(struct dpvm *dpvm, int64_t n_getsys_param);

#endif
