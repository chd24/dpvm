/* dpvm: name; T15.404-T20.174; $DVS:time$ */

#ifndef DPVM_NAME
#define DPVM_NAME

#include <stdint.h>
#include "hash.h"
#include "init.h"

#define DPVM_SHORT_HASH_MASK 	0x7fffffffffffull
#define DPVM_SHORT_HASH_SIZE 	6
#define DPVM_WORD_SIZE_MAX	12
#define DPVM_NAME_SIZE_MAX	47

extern int dpvm_name_init(struct dpvm *dpvm);
extern int dpvm_short_hash2name(struct dpvm *dpvm, uint64_t short_hash, char name[DPVM_NAME_SIZE_MAX + 1]);
extern int64_t dpvm_name2hash(struct dpvm *dpvm, const char *name, struct dpvm_hash *hash);

#endif
