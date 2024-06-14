/* dpvm: object; T15.395-T19.631; $DVS:time$ */

#ifndef DPVM_OBJECT
#define DPVM_OBJECT

#include <stdint.h>
#include "hash.h"
#include "init.h"

enum dpvm_type_ints_array {
	DPVM_TYPE_SIZEOF_LINK_MIN,
	DPVM_TYPE_SIZEOF_INT_MIN,
	DPVM_TYPE_SIZEOF_FLOAT_MIN,
	DPVM_TYPE_SIZEOF_CODE_MIN,
	DPVM_TYPE_SIZEOF_LINK_MAX,
	DPVM_TYPE_SIZEOF_INT_MAX,
	DPVM_TYPE_SIZEOF_FLOAT_MAX,
	DPVM_TYPE_SIZEOF_CODE_MAX,
	DPVM_TYPE_N_LINKS_MIN,
	DPVM_TYPE_N_INTS_MIN,
	DPVM_TYPE_N_FLOATS_MIN,
	DPVM_TYPE_N_CODES_MIN,
	DPVM_TYPE_N_LINKS_MAX,
	DPVM_TYPE_N_INTS_MAX,
	DPVM_TYPE_N_FLOATS_MAX,
	DPVM_TYPE_N_CODES_MAX,
	DPVM_TYPE_INTS_END
};

enum dpvm_type_links_array {
	DPVM_TYPE_IN_ARGS,
	DPVM_TYPE_OUT_ARGS,
	DPVM_TYPE_DEF_SUBTYPE_0,
	DPVM_TYPE_DEF_SUBTYPE_1,
	DPVM_TYPE_SUBTYPE_0,
};

enum dpvm_object_flags {
	DPVM_OBJECT_FLAG_CHECKED	= 1,
	DPVM_OBJECT_FLAG_NOT_CHECKED	= 2,
	DPVM_OBJECT_FLAG_IN_CHECK	= 4,
	DPVM_OBJECT_FLAG_CODE_MAPPED	= 8,
	DPVM_OBJECT_FLAG_LARGE_STACK	= 0x10,
};

struct dpvm_object {
	struct dpvm_object **links;
	int64_t *ints;
	double *floats;
	uint8_t *codes;
	int64_t nlinks, nints, nfloats, ncodes;
	int64_t nlinks_max, nints_max, nfloats_max, ncodes_max;
	struct dpvm_object *type;
	struct dpvm_object *machcode;
	uint64_t hash_mark;
	int64_t nrefs;
	struct dpvm_hash hash;
	uint64_t flags;
	struct dpvm *dpvm;
};

#define dpvm_type_of_link(type, nlink) 					\
		((nlink) < (type)->nlinks - DPVM_TYPE_SUBTYPE_0 	\
		? (type)->links[(nlink) + DPVM_TYPE_SUBTYPE_0] 		\
		: (type)->links[((nlink) & 1) + DPVM_TYPE_DEF_SUBTYPE_0])

extern int dpvm_object_init(struct dpvm *dpvm);

extern struct dpvm_object *dpvm_create_object(struct dpvm_object *thread, struct dpvm_object *type,
		int64_t nlinks, int64_t nints, int64_t nfloats, int64_t ncodes);
extern struct dpvm_object *dpvm_create_type(struct dpvm_object *thread,
		int64_t nlinks_min, int64_t nints_min,
		int64_t nfloats_min,int64_t ncodes_min,
		int64_t nlinks_max, int64_t nints_max, 
		int64_t nfloats_max,int64_t ncodes_max,
		struct dpvm_object *in_args, struct dpvm_object *out_args,
		struct dpvm_object *deftype0, struct dpvm_object *deftype1,
		int64_t ntypes, struct dpvm_object **types
	);
#define dpvm_link_object(obj) __sync_add_and_fetch(&(obj)->nrefs, 1)
extern struct dpvm_object *dpvm_free_object(struct dpvm_object *thread, struct dpvm_object *obj);
extern struct dpvm_object *dpvm_unlink_object(struct dpvm_object *thread, struct dpvm_object *obj);

extern int dpvm_account_memory(struct dpvm *dpvm, struct dpvm_object *thread, int64_t size);
extern int dpvm_reserve_links(struct dpvm_object *thread, struct dpvm_object *obj, int64_t n);
extern int dpvm_reserve_ints(struct dpvm_object *thread, struct dpvm_object *obj, int64_t n);
extern int dpvm_reserve_floats(struct dpvm_object *thread, struct dpvm_object *obj, int64_t n);
extern int dpvm_reserve_codes(struct dpvm_object *thread, struct dpvm_object *obj, int64_t n);

extern int dpvm_match_type(struct dpvm_object *tested_type, struct dpvm_object *pattern_type);
extern int dpvm_set_link(struct dpvm_object *thread, struct dpvm_object *obj, int64_t nlink, struct dpvm_object *link);
extern int dpvm_push_link(struct dpvm_object *thread, struct dpvm_object *obj, struct dpvm_object *link);
extern int dpvm_push_int(struct dpvm_object *thread, struct dpvm_object *obj, int64_t i);
extern int dpvm_push_float(struct dpvm_object *thread, struct dpvm_object *obj, double f);
extern int dpvm_push_code(struct dpvm_object *thread, struct dpvm_object *obj, uint8_t c);

extern struct dpvm_hash *dpvm_object_hash(struct dpvm_object *obj, uint64_t hash_mark);
extern int dpvm_serialize_object(struct dpvm_object *obj, uint64_t hash_mark, void **pmem, size_t *psize);
extern struct dpvm_object *dpvm_deserialize_object(struct dpvm *dpvm, struct dpvm_object *thread, const void *mem,
		size_t size);

#endif
