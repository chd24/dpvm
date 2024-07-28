/* dpvm: object; T15.395-T20.357; $DVS:time$ */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "bytecode.h"
#include "cache.h"
#include "error.h"
#include "hash.h"
#include "malloc.h"
#include "object.h"
#include "task.h"
#include "thread.h"

#define MEMBLOCK	256ll
#define MB_LINKS	(MEMBLOCK / sizeof(struct dpvm_object *))
#define MB_INTS		(MEMBLOCK / sizeof(int64_t))
#define MB_FLOATS	(MEMBLOCK / sizeof(double))
#define MB_CODES	(MEMBLOCK / sizeof(uint8_t))

static struct dpvm_object dpvm_type_type;

static struct dpvm_object *dpvm_type_type_links[] = 
	{ &dpvm_type_type, &dpvm_type_type, &dpvm_type_type, &dpvm_type_type };

static int64_t dpvm_type_type_ints[] = {
	DPVM_HASH_SIZE, 8, 8, 1,
	DPVM_HASH_SIZE, 8, 8, 1,
	DPVM_TYPE_SUBTYPE_0, DPVM_TYPE_INTS_END, 0, 0,
	INT64_MAX, DPVM_TYPE_INTS_END, 0, 0
};

static struct dpvm_object dpvm_type_type = {
	dpvm_type_type_links,
	dpvm_type_type_ints,
	0,
	0,
	DPVM_TYPE_SUBTYPE_0, DPVM_TYPE_INTS_END, 0, 0,
	DPVM_TYPE_SUBTYPE_0, DPVM_TYPE_INTS_END, 0, 0,
	&dpvm_type_type,
	&dpvm_type_type,
	0,
	2,
	{ 0 },
	0, 0
};

static struct dpvm_object dpvm_type_any;

static struct dpvm_object *dpvm_type_any_links[] = 
	{ &dpvm_type_any, &dpvm_type_any, &dpvm_type_any, &dpvm_type_any };

static int64_t dpvm_type_any_ints[] = {
	1, 1, 1, 1,
	INT64_MAX, INT64_MAX, INT64_MAX, INT64_MAX,
	0, 0, 0, 0,
	INT64_MAX, INT64_MAX, INT64_MAX, INT64_MAX
};

static struct dpvm_object dpvm_type_any = {
	dpvm_type_any_links,
	dpvm_type_any_ints,
	0,
	0,
	DPVM_TYPE_SUBTYPE_0, DPVM_TYPE_INTS_END, 0, 0,
	DPVM_TYPE_SUBTYPE_0, DPVM_TYPE_INTS_END, 0, 0,
	&dpvm_type_type,
	&dpvm_type_any,
	0,
	1,
	{ 0 },
	0, 0
};

int dpvm_object_init(struct dpvm *dpvm) {
	dpvm->any = &dpvm_type_any;
	dpvm->hash_mark = 0;
	dpvm->nobjects = 2;
	dpvm_type_type.dpvm = dpvm;
	dpvm_type_any.dpvm = dpvm;
	return 0;
}

#define STATIC_ASSERT(test) _Static_assert((test), "(" #test ") failed")

STATIC_ASSERT(DPVM_THREAD_LINK_END > DPVM_TASK_LINK_END);
STATIC_ASSERT(DPVM_THREAD_LINK_TASK == DPVM_TASK_LINK_PARENT);

static int dpvm_request_memory(struct dpvm *dpvm, struct dpvm_object *thread, int64_t size) {
	struct dpvm_object *task, *task0, *t;

	if (thread) {
		if (thread->nlinks == DPVM_TASK_LINK_END)
			task0 = thread;
		else
			task0 = thread->links[DPVM_THREAD_LINK_TASK];

		task = task0;

		do {
			if ((int64_t)__sync_add_and_fetch(&task->ints[DPVM_TASK_INT_MEMORY], size)
					> task->ints[DPVM_TASK_INT_MEMORY_QUOTA])
				goto fail;
			t = task;
			task = task->links[DPVM_TASK_LINK_PARENT];
		} while (task != t);
	}

	__sync_add_and_fetch(&dpvm->memory, size);
	return 0;

fail:
	do {
		t = task0;
		__sync_add_and_fetch(&t->ints[DPVM_TASK_INT_MEMORY], -size);
		task0 = t->links[DPVM_TASK_LINK_PARENT];
	} while (task != t);
	return DPVM_ERROR_NO_MEMORY;
}

static void dpvm_release_memory(struct dpvm *dpvm, struct dpvm_object *thread, int64_t size) {
	if (thread) {
		struct dpvm_object *task, *t;

		if (thread->nlinks == DPVM_TASK_LINK_END)
			task = thread;
		else
			task = thread->links[DPVM_THREAD_LINK_TASK];

		do {
			__sync_add_and_fetch(&task->ints[DPVM_TASK_INT_MEMORY], -size);
			t = task;
			task = task->links[DPVM_TASK_LINK_PARENT];
		} while (task != t);
	}

	__sync_add_and_fetch(&dpvm->memory, -size);
}

int dpvm_account_memory(struct dpvm *dpvm, struct dpvm_object *thread, int64_t size) {
	if (size > 0)
		return dpvm_request_memory(dpvm, thread, size);
	if (size < 0)
		dpvm_release_memory(dpvm, thread, -size);
	return 0;
}

struct dpvm_object *dpvm_create_object(struct dpvm_object *thread, struct dpvm_object *type,
		int64_t nlinks, int64_t nints, int64_t nfloats, int64_t ncodes) {
	struct dpvm_object *obj;
	int64_t i, nlinkend = 0, nlinks_max, nints_max, nfloats_max, ncodes_max, size;

	if (type->type != &dpvm_type_type)
		return 0;
	if (!dpvm_object_hash(thread, type, -4ull))
		return 0;

	for (i = DPVM_TYPE_SIZEOF_LINK_MIN; i <= DPVM_TYPE_SIZEOF_CODE_MIN; ++i) 
		if (type->ints[i] > dpvm_type_type.ints[i])
			return 0;

	for (i = DPVM_TYPE_SIZEOF_LINK_MAX; i <= DPVM_TYPE_SIZEOF_CODE_MAX; ++i) 
		if (type->ints[i] < dpvm_type_type.ints[i])
			return 0;

	if (type->ints[DPVM_TYPE_N_LINKS_MIN] > nlinks || nlinks > type->ints[DPVM_TYPE_N_LINKS_MAX])
		return 0;
	if (type->ints[DPVM_TYPE_N_INTS_MIN] > nints || nints > type->ints[DPVM_TYPE_N_INTS_MAX])
		return 0;
	if (type->ints[DPVM_TYPE_N_FLOATS_MIN] > nfloats || nfloats > type->ints[DPVM_TYPE_N_FLOATS_MAX])
		return 0;
	if (type->ints[DPVM_TYPE_N_CODES_MIN] > ncodes || ncodes > type->ints[DPVM_TYPE_N_CODES_MAX])
		return 0;

	nlinks_max = type->ints[DPVM_TYPE_N_LINKS_MAX];
	nints_max = type->ints[DPVM_TYPE_N_INTS_MAX];
	nfloats_max = type->ints[DPVM_TYPE_N_FLOATS_MAX];
	ncodes_max = type->ints[DPVM_TYPE_N_CODES_MAX];

	if (((nlinks - 1) | (MB_LINKS - 1)) != ((nlinks_max - 1) | (MB_LINKS - 1)))
		nlinks_max = ((nlinks - 1) | (MB_LINKS - 1)) + 1;
	if (((nints - 1) | (MB_INTS - 1)) != ((nints_max - 1) | (MB_INTS - 1)))
		nints_max = ((nints - 1) | (MB_INTS - 1)) + 1;
	if (((nfloats - 1) | (MB_FLOATS - 1)) != ((nfloats_max - 1) | (MB_FLOATS - 1)))
		nfloats_max = ((nfloats - 1) | (MB_FLOATS - 1)) + 1;
	if (((ncodes - 1) | (MB_CODES - 1)) != ((ncodes_max - 1) | (MB_CODES - 1)))
		ncodes_max = ((ncodes - 1) | (MB_CODES - 1)) + 1;

	size = sizeof(struct dpvm_object)
			+ nlinks_max  * sizeof(struct dpvm_object *)
			+ nints_max   * sizeof(int64_t)
			+ nfloats_max * sizeof(double)
			+ ncodes_max  * sizeof(uint8_t);

	if (dpvm_request_memory(type->dpvm, thread, size))
		return 0;

	obj = dpvm_calloc(1, sizeof(struct dpvm_object));
	if (!obj) {
		dpvm_release_memory(type->dpvm, thread, size);
		return 0;
	}

	obj->type = type;
	obj->machcode = obj;
	obj->dpvm = type->dpvm;
        dpvm_link_object(type);
	obj->nrefs = 1;

	obj->nlinks = nlinks;
	obj->nints = nints;
	obj->nfloats = nfloats;
	obj->ncodes = ncodes;

	obj->nlinks_max = nlinks_max;
	obj->nints_max = nints_max;
	obj->nfloats_max = nfloats_max;
	obj->ncodes_max = ncodes_max;

	if (nlinks_max) {
		struct dpvm_object *t0;
		obj->links = dpvm_malloc(nlinks_max * sizeof(void *));
		if (!obj->links) goto err;
		for (i = 0; i < nlinks; ++i) {
			struct dpvm_object *t = dpvm_type_of_link(type, i);
			if (t == type) obj->links[i] = obj;
			else if (i && t == t0 && !type->dpvm->inited) {
				obj->links[i] = obj->links[i - 1];
                                dpvm_link_object(obj->links[i]);
			} else {
				obj->links[i] = dpvm_create_object(thread, t,
						t->ints[DPVM_TYPE_N_LINKS_MIN],
						t->ints[DPVM_TYPE_N_INTS_MIN],
						t->ints[DPVM_TYPE_N_FLOATS_MIN],
						t->ints[DPVM_TYPE_N_CODES_MIN]);
				if (!obj->links[i]) { nlinkend = i; goto err; }
			}
			t0 = t;
		}
		nlinkend = i;
	}

	if (nints_max) {
		obj->ints = dpvm_malloc(nints_max * sizeof(int64_t));
		if (!obj->ints) goto err;
		if (nints) memset(obj->ints, 0, nints * sizeof(int64_t));
	}

	if (nfloats_max) {
		obj->floats = dpvm_malloc(nfloats_max * sizeof(double));
		if (!obj->floats) goto err;
		for (i = 0; i < nfloats; ++i)
			obj->floats[i] = 0.0;
	}

	if (ncodes_max) {
		obj->codes = dpvm_malloc(ncodes_max);
		if (!obj->codes) goto err;
		if (ncodes) memset(obj->codes, DPVM_CODE_NOP, ncodes);
	}

	__sync_add_and_fetch(&obj->dpvm->nobjects, 1);

	return obj;

err:
	if (obj->links) {
		for (i = 0; i < nlinkend; ++i) 
			if (obj->links[i] != obj) dpvm_unlink_object(thread, obj->links[i]);
		dpvm_free(obj->links, nlinks_max * sizeof(void *));
	}

	if (obj->ints) dpvm_free(obj->ints, nints_max * sizeof(int64_t));
	if (obj->floats) dpvm_free(obj->floats, nfloats_max * sizeof(double));
	if (obj->codes) dpvm_free(obj->codes, ncodes_max);
	dpvm_unlink_object(thread, obj->type);
	dpvm_free(obj, sizeof(struct dpvm_object));
	dpvm_release_memory(type->dpvm, thread, size);
	return 0;
}

struct dpvm_object *dpvm_free_object(struct dpvm_object *thread, struct dpvm_object *obj) {
	struct dpvm *dpvm = obj->dpvm;
	int64_t i, nlinks_max, nints_max, nfloats_max, ncodes_max;

	if (thread == obj) {
		thread = thread->links[DPVM_THREAD_LINK_TASK];
		if (thread == obj)
			thread = 0;
	}

	if (obj->type != obj)
		thread = dpvm_unlink_object(thread, obj->type);

	if (obj->machcode != obj)
		thread = dpvm_unlink_object(thread, obj->machcode);

	nlinks_max = obj->nlinks_max;
	nints_max = obj->nints_max;
	nfloats_max = obj->nfloats_max;
	ncodes_max = obj->ncodes_max;

	for (i = 0; i < obj->nlinks; ++i)
		if (obj->links[i] != obj)
			thread = dpvm_unlink_object(thread, obj->links[i]);

	if (obj->links) dpvm_free(obj->links, nlinks_max * sizeof(void *));
	if (obj->ints) dpvm_free(obj->ints, nints_max * sizeof(int64_t));
	if (obj->floats) dpvm_free(obj->floats, nfloats_max * sizeof(double));
	if (obj->codes) {
		if (obj->flags & DPVM_OBJECT_FLAG_CODE_MAPPED)
			dpvm_munmap(obj->codes, obj->ncodes_max, 1);
		else
			dpvm_free(obj->codes, obj->ncodes_max);
	}

	dpvm_free(obj, sizeof(struct dpvm_object));

	dpvm_release_memory(dpvm, thread, sizeof(struct dpvm_object)
			+ nlinks_max  * sizeof(struct dpvm_object *)
			+ nints_max   * sizeof(int64_t)
			+ nfloats_max * sizeof(double)
			+ ncodes_max  * sizeof(uint8_t));
	__sync_add_and_fetch(&dpvm->nobjects, -1);
	return thread;
}

struct dpvm_object *dpvm_unlink_object(struct dpvm_object *thread, struct dpvm_object *obj) {
	if (!__sync_add_and_fetch(&obj->nrefs, -1))
		thread = dpvm_free_object(thread, obj);
	return thread;
}

int dpvm_match_type(struct dpvm_object *thread, struct dpvm_object *tested_type, struct dpvm_object *pattern_type) {
	struct dpvm_hash *ht, *hp;

	if (tested_type == pattern_type)
		return 1;
	if (pattern_type == &dpvm_type_any)
		return 1;

	ht = dpvm_object_hash(thread, tested_type, -3ull);
	hp = dpvm_object_hash(thread, pattern_type, -3ull);

	if (ht && hp && !memcmp(ht, hp, sizeof(struct dpvm_hash)))
		return 1;

	return 0;
}

int dpvm_set_link(struct dpvm_object *thread, struct dpvm_object *obj, int64_t nlink, struct dpvm_object *link) {
	struct dpvm_object *type;
	if (nlink < 0 || nlink >= obj->nlinks)
		return DPVM_ERROR_LINKS_INDEX;
	type = dpvm_type_of_link(obj->type, nlink);
	if (!dpvm_match_type(thread, link->type, type))
		return DPVM_ERROR_TYPE_MISMATCH;
	if (link != obj) 
                dpvm_link_object(link);
	if (obj->links[nlink] != obj)
		dpvm_unlink_object(thread, obj->links[nlink]);
	obj->links[nlink] = link;
	return 0;
}

int dpvm_reserve_links(struct dpvm_object *thread, struct dpvm_object *obj, int64_t n) {
	n += obj->nlinks;
	if (n > obj->type->ints[DPVM_TYPE_N_LINKS_MAX])
		return DPVM_ERROR_LINKS_OVERFLOW;	
	if (n > obj->nlinks_max) {
		int64_t m0 = obj->nlinks_max, m = m0, size;
		void *res;
		int err;
		if (!m) m = MB_LINKS;
		while (n > m) m <<= 1;
		if (m > obj->type->ints[DPVM_TYPE_N_LINKS_MAX])
			m = obj->type->ints[DPVM_TYPE_N_LINKS_MAX];
		size = (m - m0) * sizeof(struct dpvm_object *);
		if ((err = dpvm_request_memory(obj->dpvm, thread, size)))
			return err;
		res = dpvm_realloc(obj->links, m0 * sizeof(struct dpvm_object *), m * sizeof(struct dpvm_object *));
		if (!res) {
			dpvm_release_memory(obj->dpvm, thread, size);
			return DPVM_ERROR_NO_MEMORY;
		}
		obj->links = res;
		obj->nlinks_max = m;
	}
	return 0;
}

int dpvm_push_link(struct dpvm_object *thread, struct dpvm_object *obj, struct dpvm_object *link) {
	struct dpvm_object *type;
	int res = dpvm_reserve_links(thread, obj, 1);
	if (res) return res;
	type = dpvm_type_of_link(obj->type, obj->nlinks);
	if (!dpvm_match_type(thread, link->type, type))
		return DPVM_ERROR_TYPE_MISMATCH;
	obj->links[obj->nlinks++] = link;
	if (link != obj)
                dpvm_link_object(link);
	return 0;
}

int dpvm_reserve_ints(struct dpvm_object *thread, struct dpvm_object *obj, int64_t n) {
	n += obj->nints;
	if (n > obj->type->ints[DPVM_TYPE_N_INTS_MAX])
		return DPVM_ERROR_INTS_OVERFLOW;
	if (n > obj->nints_max) {
		int64_t m0 = obj->nints_max, m = m0, size;
		void *res;
		int err;
		if (!m) m = MB_INTS;
		while (n > m) m <<= 1;
		if (m > obj->type->ints[DPVM_TYPE_N_INTS_MAX])
			m = obj->type->ints[DPVM_TYPE_N_INTS_MAX];
		size = (m -  m0) * sizeof(int64_t);
		if ((err = dpvm_request_memory(obj->dpvm, thread, size)))
			return err;
		res = dpvm_realloc(obj->ints, m0 * sizeof(int64_t), m * sizeof(int64_t));
		if (!res) {
			dpvm_release_memory(obj->dpvm, thread, size);
			return DPVM_ERROR_NO_MEMORY;
		}
		obj->ints = res;
		obj->nints_max = m;
	}
	return 0;
}

int dpvm_push_int(struct dpvm_object *thread, struct dpvm_object *obj, int64_t i) {
	int res = dpvm_reserve_ints(thread, obj, 1);
	if (!res) obj->ints[obj->nints++] = i;
	return res;
}

int dpvm_reserve_floats(struct dpvm_object *thread, struct dpvm_object *obj, int64_t n) {
	n += obj->nfloats;
	if (n > obj->type->ints[DPVM_TYPE_N_FLOATS_MAX])
		return DPVM_ERROR_FLOATS_OVERFLOW;
	if (n > obj->nfloats_max) {
		int64_t m0 = obj->nfloats_max, m = m0, size;
		void *res;
		int err;
		if (!m) m = MB_FLOATS;
		while (n > m) m <<= 1;
		if (m > obj->type->ints[DPVM_TYPE_N_FLOATS_MAX])
			m = obj->type->ints[DPVM_TYPE_N_FLOATS_MAX];
		size = (m - m0) * sizeof(double);
		if ((err = dpvm_request_memory(obj->dpvm, thread, size)))
			return err;
		res = dpvm_realloc(obj->floats, m0 * sizeof(double), m * sizeof(double));
		if (!res) {
			dpvm_release_memory(obj->dpvm, thread, size);
			return DPVM_ERROR_NO_MEMORY;
		}
		obj->floats = res;
		obj->nfloats_max = m;
	}
	return 0;
}

int dpvm_push_float(struct dpvm_object *thread, struct dpvm_object *obj, double f) {
	int res = dpvm_reserve_floats(thread, obj, 1);
	if (!res) obj->floats[obj->nfloats++] = f;
	return res;
}

int dpvm_reserve_codes(struct dpvm_object *thread, struct dpvm_object *obj, int64_t n) {
	n += obj->ncodes;
	if (n > obj->type->ints[DPVM_TYPE_N_CODES_MAX])
		return DPVM_ERROR_CODES_OVERFLOW;
	if (n > obj->ncodes_max) {
		int64_t m0 = obj->ncodes_max, m = m0, size;
		void *res;
		int err;
		if (!m) m = MB_CODES;
		while (n > m) m <<= 1;
		if (m > obj->type->ints[DPVM_TYPE_N_CODES_MAX])
			m = obj->type->ints[DPVM_TYPE_N_CODES_MAX];
		size = (m - m0) * sizeof(uint8_t);
		if ((err = dpvm_request_memory(obj->dpvm, thread, size)))
			return err;
		res = dpvm_realloc(obj->codes, m0 * sizeof(uint8_t), m * sizeof(uint8_t));
		if (!res) {
			dpvm_release_memory(obj->dpvm, thread, size);
			return DPVM_ERROR_NO_MEMORY;
		}
		obj->codes = res;
		obj->ncodes_max = m;
	}
	return 0;
}

int dpvm_push_code(struct dpvm_object *thread, struct dpvm_object *obj, uint8_t c) {
	int res = dpvm_reserve_codes(thread, obj, 1);
	if (!res) obj->codes[obj->ncodes++] = c;
	return res;
}

struct dpvm_object *dpvm_create_type(struct dpvm_object *thread,
		int64_t nlinks_min, int64_t nints_min,
		int64_t nfloats_min,int64_t ncodes_min,
		int64_t nlinks_max, int64_t nints_max, 
		int64_t nfloats_max,int64_t ncodes_max,
		struct dpvm_object *in_args, struct dpvm_object *out_args,
		struct dpvm_object *deftype0, struct dpvm_object *deftype1,
		int64_t ntypes, struct dpvm_object **types
		) {
	struct dpvm_object *type = dpvm_create_object(thread, &dpvm_type_type,
			ntypes + DPVM_TYPE_SUBTYPE_0, DPVM_TYPE_INTS_END, 0, 0);
	int err = 0;
	if (!type)
		return 0;

	memcpy(type->ints, dpvm_type_type.ints, DPVM_TYPE_INTS_END * sizeof(int64_t));
	type->ints[DPVM_TYPE_N_LINKS_MIN] = nlinks_min;
	type->ints[DPVM_TYPE_N_INTS_MIN] = nints_min;
	type->ints[DPVM_TYPE_N_FLOATS_MIN] = nfloats_min;
	type->ints[DPVM_TYPE_N_CODES_MIN] = ncodes_min;
	type->ints[DPVM_TYPE_N_LINKS_MAX] = nlinks_max;
	type->ints[DPVM_TYPE_N_INTS_MAX] = nints_max;
	type->ints[DPVM_TYPE_N_FLOATS_MAX] = nfloats_max;
	type->ints[DPVM_TYPE_N_CODES_MAX] = ncodes_max;

	err |= dpvm_set_link(thread, type, DPVM_TYPE_IN_ARGS, in_args ? in_args : type);
	err |= dpvm_set_link(thread, type, DPVM_TYPE_OUT_ARGS, out_args ? out_args : type);
	err |= dpvm_set_link(thread, type, DPVM_TYPE_DEF_SUBTYPE_0, deftype0 ? deftype0 : type);
	err |= dpvm_set_link(thread, type, DPVM_TYPE_DEF_SUBTYPE_1, deftype1 ? deftype1 : type);

	if (ntypes && types) {
		int64_t i;
		for (i = 0; i < ntypes; ++i)
			err |= dpvm_set_link(thread, type, i + DPVM_TYPE_SUBTYPE_0, types[i] ? types[i] : type);
	}

	if (err) {
		dpvm_unlink_object(thread, type);
		return 0;
	}

	return type;
}

static struct dpvm_hash *object_hash(struct dpvm_object *thread, struct dpvm_object *obj, uint64_t temp_mark, uint64_t final_mark) {
	static const struct dpvm_hash zero = { 0 };
	struct dpvm_hash_state state;
	const struct dpvm_hash *hash;
	int64_t i;
	int calculate;

	if (obj->hash_mark >= final_mark) {
		if (obj->hash_mark == -4ull)
			final_mark = -2ull;
		else
			return &obj->hash;
	}

	if ((int64_t)obj->hash_mark >= 0) {
		if (obj->hash_mark == temp_mark)
			return 0;
		obj->hash_mark = temp_mark;
	}

	calculate = final_mark != -4ull && obj->hash_mark < -2ull;

	if (calculate) {
		dpvm_hash_init(&state);
		dpvm_hash_add(obj->dpvm, thread, &state, dpvm_type_type_ints, 4 * sizeof(int64_t));
		dpvm_hash_add(obj->dpvm, thread, &state, &obj->nlinks, 4 * sizeof(int64_t));
	}

	hash = (obj->type != obj ? object_hash(thread, obj->type, temp_mark, final_mark) : &zero);
	if (!hash) return 0;

	if (calculate)
		dpvm_hash_add(obj->dpvm, thread, &state, hash, DPVM_HASH_SIZE);

	for (i = 0; i < obj->nlinks; ++i) {
		hash = (obj->links[i] != obj ? object_hash(thread, obj->links[i], temp_mark, final_mark) : &zero);
		if (!hash) return 0;
		if (calculate)
			dpvm_hash_add(obj->dpvm, thread, &state, hash, DPVM_HASH_SIZE);
	}

	if (calculate) {
		dpvm_hash_add(obj->dpvm, thread, &state, obj->ints, obj->nints * sizeof(int64_t));
		dpvm_hash_add(obj->dpvm, thread, &state, obj->floats, obj->nfloats * sizeof(double));
		dpvm_hash_add(obj->dpvm, thread, &state, obj->codes, obj->ncodes);
		dpvm_hash_final(obj->dpvm, thread, &state, &obj->hash);
	}

	obj->hash_mark = final_mark;
	return &obj->hash;
}

struct dpvm_hash *dpvm_object_hash(struct dpvm_object *thread, struct dpvm_object *obj, uint64_t hash_mark) {
	uint64_t temp_mark;
	if (obj->hash_mark >= hash_mark)
		return &obj->hash;
	temp_mark = ((int64_t)obj->hash_mark < 0 ? 0 : __sync_add_and_fetch(&obj->dpvm->hash_mark, 1));
	if (hash_mark == -3ull)
		hash_mark = (obj->hash_mark == -4ull ? -2ull : __sync_add_and_fetch(&obj->dpvm->hash_mark, 1));
	return object_hash(thread, obj, temp_mark, hash_mark);
}

int dpvm_serialize_object(struct dpvm_object *thread, struct dpvm_object *obj, uint64_t hash_mark, void **pmem, size_t *psize) {
	size_t size;
	void *mem;
	uint8_t *ptr;
	int i;

	size = (1 + obj->nlinks) * DPVM_HASH_SIZE
		+ (8 + obj->nints) * sizeof(int64_t)
		+ obj->nfloats * sizeof(double)
		+ obj->ncodes;

	if (*pmem && size <= *psize) {
		mem = *pmem;
	} else {
		mem = malloc(size);
		if (!mem)
			return -1;
	}
	
	ptr = mem;
	memcpy(ptr, dpvm_type_type_ints, 4 * sizeof(int64_t));
	ptr += 4 * sizeof(int64_t);
	memcpy(ptr, &obj->nlinks, 4 * sizeof(int64_t));
	ptr += 4 * sizeof(int64_t);
	if (obj->type != obj) {
		dpvm_object_hash(thread, obj->type, hash_mark);
		memcpy(ptr, &obj->type->hash, DPVM_HASH_SIZE);
	} else {
		memset(ptr, 0, DPVM_HASH_SIZE);
	}
	ptr += DPVM_HASH_SIZE;
	
	for (i = 0; i < obj->nlinks; ++i) {
		if (obj->links[i] != obj) {
			dpvm_object_hash(thread, obj->links[i], hash_mark);
			memcpy(ptr, &obj->links[i]->hash, DPVM_HASH_SIZE);
		} else {
			memset(ptr, 0, DPVM_HASH_SIZE);
		}
		ptr += DPVM_HASH_SIZE;
	}
	
	memcpy(ptr, obj->ints, obj->nints * sizeof(int64_t));
	ptr += obj->nints * sizeof(int64_t);
	memcpy(ptr, obj->floats, obj->nfloats * sizeof(double));
	ptr += obj->nfloats * sizeof(double);
	memcpy(ptr, obj->codes, obj->ncodes);
	
	*pmem = mem;
	*psize = size;
	return 0;
}

struct dpvm_object *dpvm_deserialize_object(struct dpvm *dpvm, struct dpvm_object *thread, const void *mem, size_t size) {
	struct dpvm_object *type, *obj;
	const int64_t *n = (const int64_t *)mem;
	int64_t nlinks, nints, nfloats, ncodes, i;
	const struct dpvm_hash *hash, zero = { 0 };
	const uint8_t *ptr;
	int err;

	if (size < DPVM_HASH_SIZE + 8 * sizeof(int64_t))
		return 0;
	if (memcmp(n, dpvm_type_type_ints, 4 * sizeof(int64_t)))
		return 0;
	n += 4;

	nlinks  = *n++;
	nints   = *n++;
	nfloats = *n++;
	ncodes  = *n++;
	if (size != (1 + nlinks) * DPVM_HASH_SIZE
			+ (8 + nints) * sizeof(int64_t)
			+ nfloats * sizeof(double)
			+ ncodes)
		return 0;
	hash = (struct dpvm_hash *)n;
	
	type = dpvm_hash2object(dpvm, thread, hash);
	if (!type) return 0;
	hash++;

	obj = dpvm_create_object(thread, type, nlinks, nints, nfloats, ncodes);
	dpvm_unlink_object(thread, type);
	if (!obj) return 0;

	for (i = 0; i < nlinks; ++i, ++hash) {
		if (memcmp(hash, &zero, sizeof(struct dpvm_hash))) {
			type = dpvm_hash2object(dpvm, thread, hash);
			if (!type) { dpvm_unlink_object(thread, obj); return 0; }
		} else type = obj;
		err = dpvm_set_link(thread, obj, i, type);
		if (type != obj) dpvm_unlink_object(thread, type);
		if (err) { dpvm_unlink_object(thread, obj); return 0; }
	}

	ptr = (uint8_t *)hash;
	memcpy(obj->ints, ptr, nints * sizeof(int64_t));
	ptr += nints * sizeof(int64_t);	
	memcpy(obj->floats, ptr, nfloats * sizeof(double));
	ptr += nfloats * sizeof(double);	
	memcpy(obj->codes, ptr, ncodes);

	return obj;
}
