/* dpvm: hash; T15.395-T20.360; $DVS:time$ */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cache.h"
#include "error.h"
#include "hash.h"
#include "object.h"
#include "run.h"
#include "store.h"
#include "thread.h"

#define NSTEPS_PER_CODE_LOG 	30

struct dpvm_hash_global {
	struct dpvm_object *compress;
};

int64_t dpvm_hash_global_init(struct dpvm *dpvm) {
	const struct dpvm_hash compress_hash = {{
		0xd098b1a123c2ae14,0x0c056fba23a8898f,
		0xa9a2a05cf2c59643,0xcc9a725a303ddf3b
	}};
	struct dpvm_object *intype, *outtype, *functype, *compress;
	void *mem = 0;
	size_t size = 0;
	int64_t err;

	if (sizeof(struct dpvm_hash_state) != DPVM_HASH_STATE_SIZE) {
		printf("Error: sizeof(struct dpvm_hash_state) == %ld != %d\n", sizeof(struct dpvm_hash_state), DPVM_HASH_STATE_SIZE);
		return 1;
	}

	dpvm->hash = calloc(sizeof(struct dpvm_hash_global), 1);
	if (!dpvm->hash) return 2;

	intype = dpvm_create_type(0, 0, 27, 0, 0, 0, 27, 0, 0, 0, 0, 0, 0, 0, 0);
	if (!intype) return 3;

	outtype = dpvm_create_type(0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0);
	if (!outtype) return 4;

	functype = dpvm_create_type(0, 0, 0, 0, 0, INT64_MAX, INT64_MAX, INT64_MAX, INT64_MAX,
			intype, outtype, dpvm->any, dpvm->any, 0, 0);
	if (!functype) return 5;

	if (dpvm_store_load(dpvm, 0, &compress_hash, DPVM_HASH_SIZE, &mem, &size))
		return 6;

	if (size < 64 + DPVM_HASH_SIZE || size != 64 + DPVM_HASH_SIZE + ((uint64_t *)mem)[5] * 8 + ((uint64_t *)mem)[7])
		return 7;

	memcpy(&functype->hash, (char *)mem + 64, DPVM_HASH_SIZE);
	functype->hash_mark = -2ull;
	compress = dpvm_create_object(0, functype, 0, ((uint64_t *)mem)[5], 0, ((uint64_t *)mem)[7]);
	dpvm_unlink_object(0, intype);
	dpvm_unlink_object(0, outtype);
	dpvm_unlink_object(0, functype);
	if (!compress)
		return 8;
	memcpy(compress->ints, (char *)mem + 64 + DPVM_HASH_SIZE, ((uint64_t *)mem)[5] * 8);
	memcpy(compress->codes, (char *)mem + 64 + DPVM_HASH_SIZE + ((uint64_t *)mem)[5] * 8, ((uint64_t *)mem)[7]);
	memcpy(&compress->hash, &compress_hash, DPVM_HASH_SIZE);
	compress->hash_mark = -2ull;
	dpvm->hash->compress = compress;

	free(mem);

	err = dpvm_cache_add(dpvm, 0, &dpvm->any);
	if (err)
		return err;

	dpvm->hash->compress = dpvm_hash2object(dpvm, 0, &compress_hash);
	dpvm_unlink_object(0, compress);
	if (!dpvm->hash->compress)
		return 9;

	return 0;
}	

void dpvm_hash_init(struct dpvm_hash_state *state) {
	state->inputSize = 0;
	state->last = 0;
	state->outLength = BLAKE2B_OUTPUT_SIZE;
}

static int64_t compress(struct dpvm *dpvm, struct dpvm_object *parent, struct dpvm_hash_state *state) {
	struct dpvm_object *compress = dpvm->hash->compress, *arg = 0, *thread = 0;
	int64_t err;

	if (!compress)
		return DPVM_ERROR_STORE_ERROR;

	arg = dpvm_create_object(parent, compress->type->links[DPVM_TYPE_IN_ARGS], 0, 27, 0, 0);
	if (!arg) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }
	memcpy(arg->ints, state, arg->nints * 8);

	thread = dpvm_thread_create(compress, arg, 0, 0, parent, DPVM_THREAD_FLAG_INTERNAL, 0, 0, 0, 0, 0);
	if (!thread) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }
	dpvm_unlink_object(parent, arg); arg = 0;

	err = dpvm_thread_start(thread, NULL);
	if (err) goto end;

	err = dpvm_run_thread(thread, compress->ncodes << NSTEPS_PER_CODE_LOG);

	if (err != DPVM_ERROR_FINISHED)
		goto end;

	err = dpvm_thread_get_result(&thread);
	if (err) goto end;

	arg = thread, thread = 0;
	memcpy(state->state, arg->ints, arg->nints * 8);
	err = 0;

end:
	if (arg) dpvm_unlink_object(parent, arg);
	if (thread) dpvm_thread_unlink(thread);
	return err;
}

int64_t dpvm_hash_add(struct dpvm *dpvm, struct dpvm_object *parent, struct dpvm_hash_state *state, const void *array, size_t size) {
	size_t ptr = 0, isize = state->inputSize & (BLAKE2B_BLOCK_SIZE - 1);
	if (state->inputSize && !isize)
		isize = BLAKE2B_BLOCK_SIZE;

	do {
		size_t todo = size - ptr;
		if (todo + isize > BLAKE2B_BLOCK_SIZE)
			todo = BLAKE2B_BLOCK_SIZE - isize;

		if (todo) {
			memcpy(state->input + isize, (char *)array + ptr, todo),
			state->inputSize += todo,
			isize += todo;
			ptr += todo;
		}

		if (isize == BLAKE2B_BLOCK_SIZE && ptr != size) {
			int64_t err = compress(dpvm, parent, state);
			if (err) return err;
			isize = 0;
		}

	} while (ptr < size);

	return 0;
}


int64_t dpvm_hash_final(struct dpvm *dpvm, struct dpvm_object *parent, struct dpvm_hash_state *state, struct dpvm_hash *hash) {
	size_t isize = state->inputSize & (BLAKE2B_BLOCK_SIZE - 1), todo = BLAKE2B_BLOCK_SIZE - isize;
	int64_t err;
	if (state->inputSize && !isize)
		isize = BLAKE2B_BLOCK_SIZE, todo = 0;

	if (todo)
		memset(state->input + isize, 0, todo);
	state->last = 1;
	err = compress(dpvm, parent, state);
	if (err) return err;

	state->inputSize = BLAKE2B_OUTPUT_SIZE;
	state->outLength = DPVM_HASH_SIZE;
	memcpy(state->input, state->state, BLAKE2B_OUTPUT_SIZE);
	memset(state->input + BLAKE2B_OUTPUT_SIZE, 0, BLAKE2B_BLOCK_SIZE - BLAKE2B_OUTPUT_SIZE);
	err = compress(dpvm, parent, state);
	if (err) return err;

	memcpy(hash, state->state, DPVM_HASH_SIZE);
	return 0;
}

int64_t dpvm_hash_of_array(struct dpvm *dpvm, struct dpvm_object *parent, const void *array, size_t size, struct dpvm_hash *hash) {
	struct dpvm_hash_state state;
	int64_t err;

	dpvm_hash_init(&state);
	err = dpvm_hash_add(dpvm, parent, &state, array, size);
	if (!err) err = dpvm_hash_final(dpvm, parent, &state, hash);
	return err;
}
