/* dpvm: store; T15.398-T20.158; $DVS:time$ */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "error.h"
#include "malloc.h"
#include "object.h"
#include "run.h"
#include "store.h"
#include "thread.h"

#define DB_FILE			".dpvm_data.dpvmb"
#define NSTEPS_PER_CODE_LOG	30
#define DB_LEN_MIN		0x400

#ifdef __ANDROID__
#define DB_LEN_MAX		0x10000000		/* 256M */
#else
#define DB_LEN_MAX		0x40000000		/* 1G */
#endif

struct dpvm_store {
	struct dpvm_object *db;
	pthread_mutex_t mutex;
	int fd;
};

int64_t dpvm_store_init(struct dpvm *dpvm) {
	struct dpvm_object *intype, *outtype, *functype, *db;
	struct stat st;
	void *mem;
	int i, fd;
	int64_t len, err;

	dpvm->store = malloc(sizeof(struct dpvm_store));
	if (!dpvm->store) return 1;

	intype = dpvm_create_type(0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0);
	if (!intype) return 2;

	outtype = dpvm_create_type(0, 1, 2, 0, 0, 1, 2, 0, 0, 0, 0, 0, 0, 1, &dpvm->any);
	if (!outtype) return 3;

	functype = dpvm_create_type(0, 0, 0, 0, 0, INT64_MAX, INT64_MAX, INT64_MAX, INT64_MAX,
			intype, outtype, dpvm->any, dpvm->any, 0, 0);
	if (!functype) return 4;

	db = dpvm_create_object(0, functype, 0, 0, 0, 0);
	if (!db) return 5;

	dpvm_unlink_object(0, intype);
	dpvm_unlink_object(0, outtype);
	dpvm_unlink_object(0, functype);

	fd = open(DB_FILE, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Database file '%s' not found", DB_FILE);
		return 6;
	}

	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "Database file '%s' stat failed", DB_FILE);
		return 7;
	}

	len = st.st_size;
	if (len < DB_LEN_MIN || len > DB_LEN_MAX) {
		fprintf(stderr, "Database file '%s' has too small or too big length", DB_FILE);
		return 8;
	}

	mem = dpvm_mmap_file(DB_LEN_MAX, fd);
	if (!mem) {
		fprintf(stderr, "Database file '%s' mmap failed", DB_FILE);
		return 9;
	}

	db->codes = mem;
	db->ncodes = len;
	db->ncodes_max = DB_LEN_MAX;
	dpvm->store->db = db;
	dpvm->store->fd = fd;
	pthread_mutex_init(&dpvm->store->mutex, NULL);

	return 0;
}

static int store_update(struct dpvm_store *store) {
	struct stat st;
	int64_t len;

	if (fstat(store->fd, &st) < 0) {
		return -1;
	}

	len = st.st_size;
	if (len > DB_LEN_MAX)
		len = DB_LEN_MAX;

	store->db->ncodes = len;
	return 0;
}

int dpvm_store_save(struct dpvm *dpvm, struct dpvm_object *parent, const void *key, size_t key_size, const void *value, long value_size) {
	struct dpvm_object *db = dpvm->store->db, *arg = 0, *thread = 0;
	int64_t err, begin, size;
	void *arr;

	pthread_mutex_lock(&dpvm->store->mutex);

	store_update(dpvm->store);

	arg = dpvm_create_object(parent, db->type->links[DPVM_TYPE_IN_ARGS], 0, 5, 0, 0);
	if (!arg) { err = DPVM_ERROR_CREATE_OBJECT << 4 | 1; goto end; }
	memcpy(arg->ints, key, key_size);
	arg->ints[4] = value_size;

	thread = dpvm_thread_create(db, arg, 0, 0, parent, DPVM_THREAD_FLAG_INTERNAL, 0, 0, 0, 0, 0);
	if (!thread) { err = DPVM_ERROR_CREATE_OBJECT << 4 | 2; goto end; }
	dpvm_unlink_object(parent, arg); arg = 0;

	err = dpvm_thread_start(thread, NULL);
	if (err) { err = err << 4 | 3; goto end; }

	err = dpvm_run_thread(thread, db->ncodes << NSTEPS_PER_CODE_LOG);

	if (err != DPVM_ERROR_FINISHED)
		{ err = err << 4 | 4; goto end; }

	err = dpvm_thread_get_result(&thread);
	if (err) { err = err << 4 | 5; goto end; }

	arg = thread, thread = 0;
	begin = arg->ints[0];
	size = arg->ints[1];

	if (size < 0) {
		err = DPVM_ERROR_STORE_ERROR << 4 | 6;
		goto end;
	} else if (size == 0) {
		err = 0;
		goto end;
	}

	size = db->ncodes + value_size + arg->links[0]->ncodes;
	if (size > DB_LEN_MAX) {
		err = DPVM_ERROR_NO_MEMORY << 4 | 7;
		goto end;
	}

	if (ftruncate(dpvm->store->fd, size) < 0) {
		err = DPVM_ERROR_STORE_ERROR << 4 | 8;
		goto end;
	}

	fsync(dpvm->store->fd);

	memcpy(db->codes + db->ncodes, value, value_size);
	memcpy(db->codes + db->ncodes + value_size, arg->links[0]->codes, arg->links[0]->ncodes);
	db->ncodes = size;
	err = 0;

end:
	if (arg) dpvm_unlink_object(parent, arg);
	if (thread) dpvm_thread_unlink(thread);
	pthread_mutex_unlock(&dpvm->store->mutex);
	return err;
}

int dpvm_store_load(struct dpvm *dpvm, struct dpvm_object *parent, const void *key, size_t key_size, void **pvalue, size_t *pvalue_size) {
	struct dpvm_object *db = dpvm->store->db, *arg = 0, *thread = 0;
	int64_t err, begin, size;
	void *arr;

	pthread_mutex_lock(&dpvm->store->mutex);

	store_update(dpvm->store);

	arg = dpvm_create_object(parent, db->type->links[DPVM_TYPE_IN_ARGS], 0, 5, 0, 0);
	if (!arg) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }
	memcpy(arg->ints, key, key_size);

	thread = dpvm_thread_create(db, arg, 0, 0, parent, DPVM_THREAD_FLAG_INTERNAL, 0, 0, 0, 0, 0);
	if (!thread) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }
	dpvm_unlink_object(parent, arg); arg = 0;

	err = dpvm_thread_start(thread, NULL);
	if (err) goto end;

	err = dpvm_run_thread(thread, db->ncodes << NSTEPS_PER_CODE_LOG);

	if (err != DPVM_ERROR_FINISHED)
		goto end;

	err = dpvm_thread_get_result(&thread);
	if (err) goto end;

	arg = thread, thread = 0;
	begin = arg->ints[0];
	size = arg->ints[1];

	if (size < 0) {
		err = DPVM_ERROR_STORE_ERROR;
		goto end;
	}

	if (*pvalue && size <= *pvalue_size) {
		arr = *pvalue;
	} else {
		arr = malloc(size);
		if (!arr) {
			err = DPVM_ERROR_NO_MEMORY;
			goto end;
		}
	}

	memcpy(arr, db->codes + begin, size);
	*pvalue = arr;
	*pvalue_size = size;
	err = 0;

end:
	if (arg) dpvm_unlink_object(parent, arg);
	if (thread) dpvm_thread_unlink(thread);
	pthread_mutex_unlock(&dpvm->store->mutex);
	return err;
}

int dpvm_store_finish(struct dpvm *dpvm) {
	pthread_mutex_destroy(&dpvm->store->mutex);
	dpvm_munmap(dpvm->store->db->codes, dpvm->store->db->ncodes_max, 0);
	dpvm->store->db->codes = 0;
	dpvm->store->db->ncodes = 0;
	dpvm->store->db->ncodes_max = 0;
	dpvm_unlink_object(0, dpvm->store->db);
	close(dpvm->store->fd);
	free(dpvm->store);
	dpvm->store = 0;
	return 0;
}
