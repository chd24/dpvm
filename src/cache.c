/* dpvm: cache; T15.401-T20.357; $DVS:time$ */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "cache.h"
#include "error.h"
#include "name.h"
#include "object.h"
#include "store.h"

#define N_BUCKETS 	0x10000
#define BUCKET_SIZE	0x10

struct dpvm_cache {
	struct dpvm_object *cache;
	pthread_mutex_t mutex;
};

static uint64_t rotate_order(uint64_t order, int i) {
	if (i) {
		int off = (i == BUCKET_SIZE ? i - 1 : i) << 2;
		uint64_t mask0, mask1, mask2;
		mask0 = -1ull << off;
		mask1 = ~mask0,
		mask0 <<= 4;
		mask2 = ~(mask0 | mask1);
		order = (order & mask0) | (order & mask1) << 4 | (order & mask2) >> off;
	}
	return order;
}

int dpvm_cache_add(struct dpvm *dpvm, struct dpvm_object *thread, struct dpvm_object **pobj) {
	struct dpvm_hash *hash;
	uint64_t short_hash, index;
	struct dpvm_object *obj = *pobj, **bucket;
	int64_t *order;
	uint64_t ord;
	int i, store = 0, err;

	if (obj->hash_mark != -1ull) {
		int64_t i;
		if (obj->type != obj && (err = dpvm_cache_add(dpvm, thread, &obj->type)))
			return err;
		for (i = 0; i < obj->nlinks; ++i)
			if (obj->links[i] != obj && (err = dpvm_cache_add(dpvm, thread, &obj->links[i])))
				return err;
		store = 1;
	}

	err = 0;
	hash = dpvm_object_hash(thread, obj, -1ull);
	if (!hash)
		return DPVM_ERROR_NOT_FINISHED;
	short_hash = hash->hash[0] & DPVM_SHORT_HASH_MASK;
	index = short_hash & (N_BUCKETS - 1);
	bucket = dpvm->cache->cache->links + index * BUCKET_SIZE;
	order = dpvm->cache->cache->ints + index;

	pthread_mutex_lock(&dpvm->cache->mutex);
	ord = *order;

	for (i = 0; i < BUCKET_SIZE; ++i, ord >>= 4)
		if ((bucket[ord & 0xf]->hash.hash[0] & DPVM_SHORT_HASH_MASK) == short_hash)
			break;

	ord = *order = rotate_order(*order, i);

	if (i == BUCKET_SIZE) {
		dpvm_set_link(thread, dpvm->cache->cache, index * BUCKET_SIZE + (ord & 0xf), obj);
		if (store) {
			size_t size = 0;
			void *mem = 0;
			if (!dpvm_store_load(dpvm, thread, hash, DPVM_HASH_SIZE, &mem, &size)) {
				struct dpvm_hash hash1;
				char name[DPVM_NAME_SIZE_MAX + 1];
				dpvm_hash_of_array(dpvm, thread, mem, size, &hash1);
				free(mem);
				if (!memcmp(hash, &hash1, DPVM_HASH_SIZE))
					{ err = 0; goto end; }
				fprintf(stderr, "\n!!! Error: object %s corrupted or mismatched in the store.\n",
						dpvm_object2name(dpvm, thread, obj, name));
				fflush(stderr);
				/* continue to store correct version */
				mem = 0;
				size = 0;
			}
			if (dpvm_serialize_object(thread, obj, -1ull, &mem, &size))
				{ free(mem); err = -1; goto end; }
			if ((err = dpvm_store_save(dpvm, thread, hash, DPVM_HASH_SIZE, mem, size)))
				{ free(mem); goto end; }
			free(mem);
		}
	} else if (obj != bucket[ord & 0xf]) {
		*pobj = bucket[ord & 0xf];
		__sync_add_and_fetch(&(*pobj)->nrefs, 1);
		dpvm_unlink_object(thread, obj);
	}

end:	
	pthread_mutex_unlock(&dpvm->cache->mutex);
	return err;
}

int64_t dpvm_cache_init(struct dpvm *dpvm) {
	struct dpvm_object *type;
	int i;
	int64_t err;

	if ((err = dpvm_store_init(dpvm)))
		return err << 4 | 1;

	type = dpvm_create_type(0, N_BUCKETS * BUCKET_SIZE, N_BUCKETS, 0, 0, N_BUCKETS * BUCKET_SIZE, N_BUCKETS, 0, 0,
			dpvm->any, dpvm->any, dpvm->any, dpvm->any, 0, 0);
	if (!type) {
		printf("Error: can't create cache type\n");
		return 2;
	}

	dpvm->cache = calloc(sizeof(struct dpvm_cache), 1);
	if (!dpvm->cache) {
		printf("Error: can't create cache struct\n");
		return 3;
	}
	pthread_mutex_init(&dpvm->cache->mutex, NULL);

	dpvm->cache->cache = dpvm_create_object(0, type, N_BUCKETS * BUCKET_SIZE, N_BUCKETS, 0, 0);
	if (!dpvm->cache->cache) {
		printf("Error: can't create cache object\n");
		return 4;
	}
	
	for (i = 0; i < N_BUCKETS; ++i)
		dpvm->cache->cache->ints[i] = 0xfedcba9876543210llu;

	return 0;
}

int64_t dpvm_cache_post_init(struct dpvm *dpvm) {
	dpvm_cache_add(dpvm, 0, &dpvm->cache->cache->type);

	return 0;
}

void dpvm_cache_finish(struct dpvm *dpvm) {
	dpvm_store_finish(dpvm);	
}

char *dpvm_object2name(struct dpvm *dpvm, struct dpvm_object *thread, struct dpvm_object *obj, char name[DPVM_NAME_SIZE_MAX + 1]) {
	struct dpvm_hash *hash = dpvm_object_hash(thread, obj, -3ull);
	if (!hash)
		return 0;
	if (dpvm_short_hash2name(dpvm, hash->hash[0] & DPVM_SHORT_HASH_MASK, name) < 0)
		return 0;
	return name;
}

struct dpvm_object *dpvm_short_hash2object(struct dpvm *dpvm, struct dpvm_object *thread, int64_t short_hash) {
	int64_t index;
	struct dpvm_object *obj, **bucket;
	int64_t *order;
	uint64_t ord;
	int i, store = 0;
	void *mem = 0;
	size_t size = 0;

	index = short_hash & (N_BUCKETS - 1);
	bucket = dpvm->cache->cache->links + index * BUCKET_SIZE;
	order = dpvm->cache->cache->ints + index;

	pthread_mutex_lock(&dpvm->cache->mutex);
	ord = *order;

	for (i = 0; i < BUCKET_SIZE; ++i, ord >>= 4) {
		obj = bucket[ord & (BUCKET_SIZE - 1)];
		if ((obj->hash.hash[0] & DPVM_SHORT_HASH_MASK) == short_hash) {
			ord = *order = rotate_order(*order, i);
			__sync_add_and_fetch(&obj->nrefs, 1);
			pthread_mutex_unlock(&dpvm->cache->mutex);
			return obj;
		}
	}

	if (dpvm_store_load(dpvm, thread, &short_hash, DPVM_SHORT_HASH_SIZE, &mem, &size)) {
		pthread_mutex_unlock(&dpvm->cache->mutex);
		return 0;
	}
	pthread_mutex_unlock(&dpvm->cache->mutex);

	obj = dpvm_deserialize_object(dpvm, thread, mem, size);
	free(mem);
	if (!obj)
		return 0;

	if (dpvm_cache_add(dpvm, thread, &obj))
		return 0;

	if ((obj->hash.hash[0] & DPVM_SHORT_HASH_MASK) != short_hash) {
		char name[DPVM_NAME_SIZE_MAX + 1];
		fprintf(stderr, "\n!!! Error: hash mismatch for object %s.\n", dpvm_object2name(dpvm, thread, obj, name));
		fflush(stderr);
		dpvm_free_object(thread, obj);
		return 0;
	}

	return obj;
}

struct dpvm_object *dpvm_hash2object(struct dpvm *dpvm, struct dpvm_object *thread, const struct dpvm_hash *hash) {
	struct dpvm_object *obj, **bucket;
	int64_t index, short_hash = hash->hash[0] & DPVM_SHORT_HASH_MASK, *order;
	uint64_t ord;
	int i, store = 0;
	void *mem = 0;
	size_t size = 0;

	index = short_hash & (N_BUCKETS - 1);
	bucket = dpvm->cache->cache->links + index * BUCKET_SIZE;
	order = dpvm->cache->cache->ints + index;

	pthread_mutex_lock(&dpvm->cache->mutex);
	ord = *order;

	for (i = 0; i < BUCKET_SIZE; ++i, ord >>= 4) {
		obj = bucket[ord & (BUCKET_SIZE - 1)];
		if (!memcmp(&obj->hash, hash, DPVM_HASH_SIZE)) {
			ord = *order = rotate_order(*order, i);
			__sync_add_and_fetch(&obj->nrefs, 1);
			pthread_mutex_unlock(&dpvm->cache->mutex);
			return obj;
		}
	}

	if (dpvm_store_load(dpvm, thread, hash, DPVM_HASH_SIZE, &mem, &size)) {
		pthread_mutex_unlock(&dpvm->cache->mutex);
		return 0;
	}
	pthread_mutex_unlock(&dpvm->cache->mutex);

	obj = dpvm_deserialize_object(dpvm, thread, mem, size);
	free(mem);
	if (!obj)
		return 0;

	if (dpvm_cache_add(dpvm, thread, &obj))
		return 0;

	if (memcmp(&obj->hash, hash, DPVM_HASH_SIZE)) {
		char name[DPVM_NAME_SIZE_MAX + 1];
		fprintf(stderr, "\n!!! Error: hash mismatch for object %s.\n", dpvm_object2name(dpvm, thread, obj, name));
		fflush(stderr);
		dpvm_free_object(thread, obj);
		return 0;
	}

	return obj;
}
