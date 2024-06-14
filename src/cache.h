/* dpvm: cache; T15.401-T20.174; $DVS:time$ */

#ifndef DPVM_CACHE
#define DPVM_CACHE

#include <stdint.h>
#include "hash.h"
#include "init.h"
#include "name.h"

extern int64_t dpvm_cache_init(struct dpvm *dpvm);
extern void dpvm_cache_finish(struct dpvm *dpvm);
extern int dpvm_cache_add(struct dpvm *dpvm, struct dpvm_object *thread, struct dpvm_object **pobj);

extern char *dpvm_object2name(struct dpvm *dpvm, struct dpvm_object *obj,
		char name[DPVM_NAME_SIZE_MAX + 1]);
extern struct dpvm_object *dpvm_short_hash2object(struct dpvm *dpvm, struct dpvm_object *thread,
		int64_t short_hash);
extern struct dpvm_object *dpvm_hash2object(struct dpvm *dpvm, struct dpvm_object *thread,
		const struct dpvm_hash *hash);

#endif
