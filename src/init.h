/* dpvm: init; T15.401-T20.355; $DVS:time$ */

#ifndef DPVM_INIT
#define DPVM_INIT

#include <stdint.h>

#define DPVM_INIT_FLAG_IO_BUF	1

struct dpvm_store;
struct dpvm_cache;
struct dpvm_hash_global;
struct dpvm_io;
struct dpvm_mailbox;
struct dpvm_object;
struct dpvm_store;
struct dpvm_task;
struct dpvm_thread;

struct dpvm {
	struct dpvm_object *any;
	struct dpvm_object *checker;
	struct dpvm_object *monitor;
	struct dpvm_object *translator;
	struct dpvm_object *words;
	struct dpvm_cache *cache;
	struct dpvm_io *io;
	struct dpvm_hash_global *hash;
	struct dpvm_mailbox *mailbox;
	struct dpvm_store *store;
	struct dpvm_task *tasks;
	struct dpvm_thread *threads;
	uint64_t memory;
	uint64_t nobjects;
	uint64_t hash_mark;
	int flags;
	int inited;
};

extern int dpvm_sigint;

extern int64_t dpvm_init(struct dpvm *dpvm, int flags);
extern int dpvm_finish(struct dpvm *dpvm);

#endif
