/* dpvm: hash; T15.395-T15.412; $DVS:time$ */

#include <stdio.h>
#include "../blake2/blake2.h"
#include "hash.h"

int dpvm_hash_global_init(void) {
	if (sizeof(struct dpvm_hash_state) < sizeof(blake2b_state)) {
		printf("Error: sizeof(blake2b_state) = %ld\n", sizeof(blake2b_state));
		return -1;
	}
	return 0;
}	

void dpvm_hash_init(struct dpvm_hash_state *state) {
	blake2b_init((blake2b_state *)state, BLAKE2B_OUTBYTES);
}

void dpvm_hash_add(struct dpvm_hash_state *state, const void *array, size_t size) {
	blake2b_update((blake2b_state *)state, array, size);
}


void dpvm_hash_final(struct dpvm_hash_state *state, struct dpvm_hash *hash) {
	uint8_t hash0[BLAKE2B_OUTBYTES];

	blake2b_final((blake2b_state *)state, hash0, BLAKE2B_OUTBYTES);
	blake2b_init((blake2b_state *)state, DPVM_HASH_SIZE);
	blake2b_update((blake2b_state *)state, hash0, BLAKE2B_OUTBYTES);
	blake2b_final((blake2b_state *)state, hash, DPVM_HASH_SIZE);
}

void dpvm_hash_of_array(const void *array, size_t size, struct dpvm_hash *hash) {
	struct dpvm_hash_state state;
	dpvm_hash_init(&state);
	dpvm_hash_add(&state, array, size);
	dpvm_hash_final(&state, hash);
}
