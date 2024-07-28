/* dpvm: hash; T15.395-T20.357; $DVS:time$ */

#ifndef DPVM_HASH
#define DPVM_HASH

#include <stdint.h>
#include <stddef.h>
#include "init.h"

#define DPVM_HASH_SIZE 		32
#define BLAKE2B_OUTPUT_SIZE	64
#define BLAKE2B_BLOCK_SIZE	128
#define DPVM_HASH_STATE_SIZE 	(27*8)
#define DPVM_HASH_STR_SIZE	8

struct dpvm_hash_state {
	uint64_t state[8];
	char input[BLAKE2B_BLOCK_SIZE];
	uint64_t inputSize;
	uint64_t last;
	uint64_t outLength;
};

struct dpvm_hash {
	uint64_t hash[DPVM_HASH_SIZE / sizeof(uint64_t)];
};

extern int64_t dpvm_hash_global_init(struct dpvm *dpvm);
extern void dpvm_hash_init(struct dpvm_hash_state *state);
extern int64_t dpvm_hash_add(struct dpvm *dpvm, struct dpvm_object *parent, struct dpvm_hash_state *state, const void *array, size_t size);
extern int64_t dpvm_hash_final(struct dpvm *dpvm, struct dpvm_object *parent, struct dpvm_hash_state *state, struct dpvm_hash *hash);
extern int64_t dpvm_hash_of_array(struct dpvm *dpvm, struct dpvm_object *parent, const void *array, size_t size, struct dpvm_hash *hash);

#endif
