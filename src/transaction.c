/* dpvm: transaction; T17.903-T19.643; $DVS:time$ */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include "cache.h"
#include "error.h"
#include "init.h"
#include "io.h"
#include "object.h"
#include "task.h"
#include "thread.h"

#define N_BUCKETS               0x1000
#define STORAGE_TIMEOUT_SHORT   60
#define STORAGE_TIMEOUT_MID     (60 * 60)
#define STORAGE_TIMEOUT_LONG    (24 * 60 * 60)

enum bucket_int_fields {
        BUCKET_FLAGS,
        BUCKET_ERROR,
        BUCKET_TIME,
        BUCKET_HASH0,
        BUCKET_HASH1,
        BUCKET_HASH2,
        BUCKET_HASH3,
        BUCKET_END
};

enum bucket_flags {
        BUCKET_FLAG_READY     = 1,
        BUCKET_FLAG_ERROR     = 2,
        BUCKET_FLAG_CANCEL    = 4,
        BUCKET_FLAG_USED      = 8,
        BUCKET_FLAG_TEMPORARY = 0x10
};

struct dpvm_transaction {
        pthread_cond_t          cond[N_BUCKETS];
        pthread_mutex_t         mutex[N_BUCKETS];
        pthread_t               clean;
        struct dpvm *           dpvm;
        struct dpvm_object *	type_transaction;
        struct dpvm_object *	type_bucket;
        struct dpvm_object *	type_hash;
        struct dpvm_object *	hash;
        struct dpvm_object *	bucket0;
        int64_t                 nalloced;
        int64_t                 nready;
        int64_t                 temporaryId;
};

static void remove_transaction(struct dpvm_transaction *t, struct dpvm_object *thread, struct dpvm_object *bucket,
		int64_t n, int64_t i) {
	struct dpvm_object *tmp = bucket->links[n];
	int64_t j;

	if (bucket->ints[i + BUCKET_FLAGS] & (BUCKET_FLAG_READY | BUCKET_FLAG_ERROR))
		__sync_add_and_fetch(&t->nready, -1);

	if (n < bucket->nlinks - 1) {
		bucket->links[n] = bucket->links[bucket->nlinks - 1];
		for (j = 0; j < BUCKET_END; ++j)
			bucket->ints[i + j] = bucket->ints[bucket->nints - BUCKET_END + j];
	}

	if (tmp != bucket)
		dpvm_unlink_object(thread, tmp);

	--bucket->nlinks;
	bucket->nints -= BUCKET_END;
	__sync_add_and_fetch(&t->nalloced, -1);
}

static void *clean_thread(void *arg) {
        struct dpvm_transaction *t = (struct dpvm_transaction *)arg;
        int h;

        for(h = 0;; ++h, h %= N_BUCKETS) {
            struct dpvm_object *bucket;
            time_t tim = time(0);
            int64_t i, n;

            sleep(1);
            pthread_mutex_lock(&t->mutex[h]);
            bucket = t->hash->links[h];

            for (i = n = 0; i < bucket->nints; ) {
                if ((bucket->ints[i + BUCKET_FLAGS] & (BUCKET_FLAG_USED | BUCKET_FLAG_CANCEL)
                        && tim > bucket->ints[i + BUCKET_TIME] + STORAGE_TIMEOUT_MID)
                        || (bucket->ints[i + BUCKET_FLAGS]
                        && tim > bucket->ints[i + BUCKET_TIME] + STORAGE_TIMEOUT_LONG)) {

                    remove_transaction(t, 0, bucket, n, i); /* TODO: memory leak from task's quota */
                } else {
                    i += BUCKET_END;
                    ++n;
                }
            }

            pthread_mutex_unlock(&t->mutex[h]);
        }

        return 0;
}

int dpvm_transaction_init(struct dpvm *dpvm, struct dpvm_transaction **ptransaction) {
        struct dpvm_transaction *t = calloc(sizeof(struct dpvm_transaction), 1);
        int i, err;

        if (!t) return -1;

        t->dpvm = dpvm;

        t->type_transaction = dpvm_create_type(0, 2, 0, 0, 0, 2, 0, 0, 0,
                dpvm->any, dpvm->any, dpvm->any, dpvm->any, 0, 0);
        if (!t->type_transaction)
                return -2;

        if ((err = dpvm_cache_add(dpvm, 0, &t->type_transaction)))
                return err;

        t->type_bucket = dpvm_create_type(0, 0, 0, 0, 0, INT64_MAX, INT64_MAX, 0, 0,
                dpvm->any, dpvm->any, dpvm->any, dpvm->any, 0, 0);
        if (!t->type_bucket)
                return -3;

        if ((err = dpvm_cache_add(dpvm, 0, &t->type_bucket)))
                return err;

        t->type_hash = dpvm_create_type(0, N_BUCKETS, 0, 0, 0, N_BUCKETS, 0, 0, 0,
                dpvm->any, dpvm->any, t->type_bucket, t->type_bucket, 0, 0);
        if (!t->type_hash)
                return -4;

        if ((err = dpvm_cache_add(dpvm, 0, &t->type_hash)))
                return err;

        t->hash = dpvm_create_object(0, t->type_hash, N_BUCKETS, 0, 0, 0);
        if (!t->hash)
                return -5;

        t->bucket0 = t->hash->links[0];
        dpvm_link_object(t->bucket0);

        for (i = 0; i < N_BUCKETS; ++i) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
                pthread_mutex_init(&t->mutex[i], &attr);
                pthread_cond_init(&t->cond[i], 0);
        }

        if (pthread_create(&t->clean, 0, &clean_thread, t))
		return -6;

        *ptransaction = t;
        return 0;
}

int dpvm_transaction_build(struct dpvm_transaction *t, struct dpvm_object *thread, struct dpvm_object *func,
		struct dpvm_object **links, struct dpvm_object **ptransaction) {
        struct dpvm_object *transaction = dpvm_create_object(thread, t->type_transaction, 2, 0, 0, 0), *arg = NULL,
		*intype = func->type->links[0], *modifiers = intype->links[3], *temporaryType = NULL, *link;
        int64_t nlinks = intype->ints[8], nints = intype->ints[9],
                nfloats = intype->ints[10], ncodes = intype->ints[11], i;
        int err, temporary = 0;

	if (!transaction)
		return DPVM_ERROR_CREATE_OBJECT;
	*ptransaction = transaction;

        if ((err = dpvm_set_link(thread, transaction, 0, func)))
                return err;

	if (intype != modifiers && modifiers->nlinks == 4 + nlinks) {
		for (i = 0; i < nlinks; ++i) {
			if (modifiers->links[i + 4]->ints[2] == 'v') {
				if ((int64_t)links[i]->hash_mark < 0 || (int64_t)links[i]->nrefs != 1)
					return DPVM_ERROR_CONST;
				intype = func->dpvm->any;
			} else if (modifiers->links[i + 4]->ints[1] == 's')
				intype = func->dpvm->any;
		}
	}

	arg = dpvm_create_object(thread, intype, nlinks, nints, nfloats, ncodes);
        if (!arg)
		return DPVM_ERROR_CREATE_OBJECT;
        err = dpvm_set_link(thread, transaction, 1, arg);
        dpvm_unlink_object(thread, arg);
	if (err)
                return err;

        for (i = 0; i < nlinks; ++i) {
	/* remove volatile parameters from transaction; if parameter marked as volatile, then it is required
	   that argument should be not const and there will be only one reference to it (except transaction) */
		if (intype == func->dpvm->any) {
			if (!temporaryType) {
				temporaryType = dpvm_create_type(thread, 4, 16, 0, 0, INT64_MAX, 16, 0,
					__sync_add_and_fetch(&t->temporaryId, 1), 0, 0, 0, 0, 0, 0);
				if (!temporaryType)
					return DPVM_ERROR_CREATE_OBJECT;
				dpvm_unlink_object(thread, temporaryType->type);
				temporaryType->type = temporaryType;
			}

			err = dpvm_set_link(thread, arg, i, temporaryType);

			if (!temporary)
				dpvm_unlink_object(thread, temporaryType);
			temporary = -1;

			if (!err && modifiers->links[i + 4]->ints[2] != 'v' && !dpvm_object_hash(links[i], -4ull))
				err = DPVM_ERROR_NOT_FINISHED;
		} else
			err = dpvm_set_link(thread, arg, i, links[i]);

		if (err)
			return err;
	}

        if (!dpvm_object_hash(transaction, -4ull))
                return DPVM_ERROR_NOT_FINISHED;

        return temporary;
}

int dpvm_transaction_add(struct dpvm_transaction *t, struct dpvm_object *thread, struct dpvm_object *transaction,
		int temporary) {
        struct dpvm_hash *hash = dpvm_object_hash(transaction, -2ull);
        struct dpvm_object *bucket;
        int64_t i, i0;
        time_t tim;
        int h, err = 0;

        if (!hash)
                return DPVM_ERROR_NOT_FINISHED;
        h = hash->hash[0] & (N_BUCKETS - 1);

        pthread_mutex_lock(&t->mutex[h]);

        bucket = t->hash->links[h];
        if (bucket == t->bucket0) {
                bucket = dpvm_create_object(thread, t->type_bucket, 0, 0, 0, 0);
                if (!bucket) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }
                t->hash->links[h] = bucket;
        }

        i0 = -1l;
        tim = time(0);
        for (i = 0; i < bucket->nints; i += BUCKET_END) {
                if (hash->hash[0] == bucket->ints[i + BUCKET_HASH0] && hash->hash[1] == bucket->ints[i + BUCKET_HASH1] &&
                    hash->hash[2] == bucket->ints[i + BUCKET_HASH2] && hash->hash[3] == bucket->ints[i + BUCKET_HASH3]) {
                        if (bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_CANCEL) {
                            bucket->ints[i + BUCKET_FLAGS] &= ~BUCKET_FLAG_CANCEL;
                            bucket->ints[i + BUCKET_ERROR] = 0;
                        } else
                            err = DPVM_ERROR_FINISHED;
                        bucket->ints[i + BUCKET_TIME] = tim;
                        if (temporary)
                            bucket->ints[i + BUCKET_FLAGS] |= BUCKET_FLAG_TEMPORARY;
                        goto end;
                }
                if (bucket->ints[i + BUCKET_FLAGS] & (BUCKET_FLAG_USED | BUCKET_FLAG_CANCEL)
                                && tim > bucket->ints[i + BUCKET_TIME] + STORAGE_TIMEOUT_SHORT
                                && (i0 < 0 || bucket->ints[i + BUCKET_TIME] < bucket->ints[i0 + BUCKET_TIME]))
                        i0 = i;
        }

        if (i0 < 0) {
                i0 = i;
                if ((err = dpvm_push_link(thread, bucket, bucket))) goto end;
                if ((err = dpvm_reserve_ints(thread, bucket, BUCKET_END))) { bucket->nlinks--; goto end; }
                bucket->nints += BUCKET_END;
                __sync_add_and_fetch(&t->nalloced, 1);                
        } else if (bucket->ints[i0 + BUCKET_FLAGS] & (BUCKET_FLAG_READY | BUCKET_FLAG_ERROR))
                __sync_add_and_fetch(&t->nready, -1);

        bucket->ints[i0 + BUCKET_FLAGS] = temporary ? BUCKET_FLAG_TEMPORARY : 0;
        bucket->ints[i0 + BUCKET_ERROR] = 0;
        bucket->ints[i0 + BUCKET_TIME] = tim;
        bucket->ints[i0 + BUCKET_HASH0] = hash->hash[0];
        bucket->ints[i0 + BUCKET_HASH1] = hash->hash[1];
        bucket->ints[i0 + BUCKET_HASH2] = hash->hash[2];
        bucket->ints[i0 + BUCKET_HASH3] = hash->hash[3];

end:
        pthread_mutex_unlock(&t->mutex[h]);
        return err;
}

int dpvm_transaction_set_result(struct dpvm_transaction *t, struct dpvm_object *thread, struct dpvm_object *transaction,
		struct dpvm_object *result, int64_t error) {
        struct dpvm_hash *hash = dpvm_object_hash(transaction, -2ull);
        struct dpvm_object *bucket;
        int64_t i, n, err;
        int h;

        dpvm_unlink_object(thread, transaction);

        if (!hash) {
		dpvm_unlink_object(thread, result);
                return DPVM_ERROR_NOT_FINISHED;
        }
        h = hash->hash[0] & (N_BUCKETS - 1);

        pthread_mutex_lock(&t->mutex[h]);

        bucket = t->hash->links[h];
        for (i = n = 0; i < bucket->nints; i += BUCKET_END, ++n) {
                if (hash->hash[0] == bucket->ints[i + BUCKET_HASH0] && hash->hash[1] == bucket->ints[i + BUCKET_HASH1] &&
                    hash->hash[2] == bucket->ints[i + BUCKET_HASH2] && hash->hash[3] == bucket->ints[i + BUCKET_HASH3]) {
                        if (bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_READY) {
				if (!error) {
					struct dpvm_hash *lhash = dpvm_object_hash(bucket->links[n],
						 bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_TEMPORARY ? -3ull : -2ull),
						 *rhash = dpvm_object_hash(result,
						 bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_TEMPORARY ? -3ull : -2ull);
					if (!lhash || !rhash) { err = DPVM_ERROR_NOT_FINISHED; goto end; }
					if (memcmp(rhash, lhash, sizeof(struct dpvm_hash)))
						err = DPVM_ERROR_TYPE_MISMATCH;
				} else if (!DPVM_ERROR_IS_RETRYABLE(error & 0x1f))
					err = DPVM_ERROR_TYPE_MISMATCH;
                        } else if (bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_ERROR) {
				if (error != bucket->ints[i + BUCKET_ERROR] && !DPVM_ERROR_IS_RETRYABLE(error & 0x1f))
					err = DPVM_ERROR_TYPE_MISMATCH;
                        } else if (!error) {
				if (!(bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_TEMPORARY)
						&& !dpvm_object_hash(result, -4ull)) {
					err = DPVM_ERROR_NOT_FINISHED;
					goto end;
				}
				if ((err = dpvm_set_link(thread, bucket, n, result))) goto end;
				bucket->ints[i + BUCKET_FLAGS] &= ~BUCKET_FLAG_CANCEL;
				bucket->ints[i + BUCKET_FLAGS] |= BUCKET_FLAG_READY;
				__sync_add_and_fetch(&t->nready, 1);
				pthread_cond_broadcast(&t->cond[h]);
                        } else if (!DPVM_ERROR_IS_RETRYABLE(error & 0x1f)) {
				bucket->ints[i + BUCKET_FLAGS] &= ~BUCKET_FLAG_CANCEL;
				bucket->ints[i + BUCKET_FLAGS] |= BUCKET_FLAG_ERROR;
				bucket->ints[i + BUCKET_ERROR] = error;
				__sync_add_and_fetch(&t->nready, 1);
				pthread_cond_broadcast(&t->cond[h]);
                        } else {
				bucket->ints[i + BUCKET_FLAGS] |= BUCKET_FLAG_CANCEL;
				bucket->ints[i + BUCKET_ERROR] = error;
				pthread_cond_broadcast(&t->cond[h]);
                        }
                        bucket->ints[i + BUCKET_TIME] = time(0);
                        goto end;
                }
        }

        err = DPVM_ERROR_END_OF_CODE;

end:
	dpvm_unlink_object(thread, result);
        pthread_mutex_unlock(&t->mutex[h]);
        return err;
}

int64_t dpvm_transaction_get_result(struct dpvm_transaction *t, struct dpvm_object *thread,
		struct dpvm_object *transaction, struct dpvm_object **presult) {
        struct dpvm_hash *hash = dpvm_object_hash(transaction, -2ull);
	struct dpvm_object *bucket;
        int64_t i, n = -1ll, err = 0;
        int h;

        if (!hash)
                return DPVM_ERROR_NOT_FINISHED;
        h = hash->hash[0] & (N_BUCKETS - 1);

        pthread_mutex_lock(&t->mutex[h]);
	bucket = t->hash->links[h];

        while (!(thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH)) {
                for (i = n = 0; i < bucket->nints; i += BUCKET_END, ++n) {
                        if (hash->hash[0] == bucket->ints[i + BUCKET_HASH0] && hash->hash[1] == bucket->ints[i + BUCKET_HASH1] &&
                            hash->hash[2] == bucket->ints[i + BUCKET_HASH2] && hash->hash[3] == bucket->ints[i + BUCKET_HASH3]) {
                                if (bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_READY) {
                                        dpvm_link_object(bucket->links[n]);
                                        *presult = bucket->links[n];
                                        bucket->ints[i + BUCKET_TIME] = time(0);
                                        bucket->ints[i + BUCKET_FLAGS] |= BUCKET_FLAG_USED;
                                        goto end;
                                } else if (bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_ERROR) {
                                        err = bucket->ints[i + BUCKET_ERROR];
                                        bucket->ints[i + BUCKET_TIME] = time(0);
                                        bucket->ints[i + BUCKET_FLAGS] |= BUCKET_FLAG_USED;
                                        goto end;
                                } else if (bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_CANCEL) {
					if (bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_TEMPORARY)
						err = bucket->ints[i + BUCKET_ERROR];
					else
						err = dpvm_thread_run_transaction(thread, transaction, NULL);
					if (err) goto end;
					bucket->ints[i + BUCKET_TIME] = time(0);
					bucket->ints[i + BUCKET_FLAGS] &= ~BUCKET_FLAG_CANCEL;
                                } else
                                        break;
                        }
                }
                {
                        uint64_t tim = dpvm_get_time() + 1000000000;
                        struct timespec ts;
                        ts.tv_sec  = tim / 1000000000;
                        ts.tv_nsec = tim % 1000000000;
                        pthread_cond_timedwait(&t->cond[h], &t->mutex[h], &ts);
                }

                n = -1ll;
        }

        err = DPVM_ERROR_FINISHED;

end:
	if (n >= 0 && bucket->ints[i + BUCKET_FLAGS] & BUCKET_FLAG_TEMPORARY)
		remove_transaction(t, thread, bucket, n, i);
        pthread_mutex_unlock(&t->mutex[h]);
        return err;
}

int64_t dpvm_transaction_get_nalloced(struct dpvm_transaction *t) {
        return t->nalloced;
}

int64_t dpvm_transaction_get_nready(struct dpvm_transaction *t) {
        return t->nready;
}

struct dpvm_object *dpvm_transaction_get_type(struct dpvm_transaction *t) {
        return t->type_transaction;
}
