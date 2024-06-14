/* dpvm: store; T15.398-T20.150; $DVS:time$ */

#ifndef DPVM_STORE
#define DPVM_STORE

#include <stddef.h>
#include "init.h"

extern int64_t dpvm_store_init(struct dpvm *dpvm);

extern int dpvm_store_save(struct dpvm *dpvm, struct dpvm_object *parent,
	const void *key, size_t key_size, const void *value, long value_size);

extern int dpvm_store_load(struct dpvm *dpvm, struct dpvm_object *parent,
	const void *key, size_t key_size, void **pvalue, size_t *pvalue_size);

extern int dpvm_store_finish(struct dpvm *dpvm);

#endif

