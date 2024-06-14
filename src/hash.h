/* dpvm: hash; T15.395-T15.412; $DVS:time$ */

#ifndef DPVM_HASH
#define DPVM_HASH

#include <stdint.h>
#include <stddef.h>

#define DPVM_HASH_SIZE 		32
#define DPVM_HASH_STATE_SIZE 	256
#define DPVM_HASH_STR_SIZE	8

struct dpvm_hash_state {
	uint64_t hash[DPVM_HASH_STATE_SIZE / sizeof(uint64_t)];
};

struct dpvm_hash {
	uint64_t hash[DPVM_HASH_SIZE / sizeof(uint64_t)];
};

extern int dpvm_hash_global_init(void);
extern void dpvm_hash_init(struct dpvm_hash_state *state);
extern void dpvm_hash_add(struct dpvm_hash_state *state, const void *array, size_t size);
extern void dpvm_hash_final(struct dpvm_hash_state *state, struct dpvm_hash *hash);
extern void dpvm_hash_of_array(const void *array, size_t size, struct dpvm_hash *hash);

#endif
