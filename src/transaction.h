/* dpvm: transaction; T17.903-T19.630; $DVS:time$ */

#ifndef DPVM_TRANSACTION
#define DPVM_TRANSACTION

#include <stdint.h>

struct dpvm;
struct dpvm_object;
struct dpvm_transaction;

extern int dpvm_transaction_init(struct dpvm *dpvm, struct dpvm_transaction **ptransaction);
extern int dpvm_transaction_build(struct dpvm_transaction *t, struct dpvm_object *thread, struct dpvm_object *func,
		struct dpvm_object **links, struct dpvm_object **ptransaction);
extern int dpvm_transaction_add(struct dpvm_transaction *t, struct dpvm_object *thread, struct dpvm_object *transaction,
		int temporary);
extern int dpvm_transaction_set_result(struct dpvm_transaction *t, struct dpvm_object *thread,
		struct dpvm_object *transaction, struct dpvm_object *result, int64_t error);
extern int64_t dpvm_transaction_get_result(struct dpvm_transaction *t, struct dpvm_object *thread,
		struct dpvm_object *transaction, struct dpvm_object **presult);
extern int64_t dpvm_transaction_get_nalloced(struct dpvm_transaction *t);
extern int64_t dpvm_transaction_get_nready(struct dpvm_transaction *t);
extern struct dpvm_object *dpvm_transaction_get_type(struct dpvm_transaction *t);

#endif
