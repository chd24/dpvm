/* dpvm: mailbox; T16.625-T20.357; $DVS:time$ */

#include <stdlib.h>
#include <pthread.h>
#include "error.h"
#include "io.h"
#include "object.h"
#include "task.h"
#include "thread.h"

#define TABLE_EXP	16
#define BUCKET_EXP	4
#define COND_EXP	(TABLE_EXP - BUCKET_EXP)

struct dpvm_mailbox {
	struct dpvm_object *mbox;
	struct dpvm_object *empty;
	pthread_mutex_t mutex[1 << COND_EXP];
	pthread_cond_t cond[1 << COND_EXP];
	int64_t naddrs;
	int64_t nmess;
};

int dpvm_mailbox_init(struct dpvm *dpvm) {
	struct dpvm_mailbox *m;
	struct dpvm_object *type;
	int i;

	dpvm->mailbox = m = calloc(sizeof(struct dpvm_mailbox), 1);
	if (!m)
		return -1;

	type = dpvm_create_type(0, 1 << TABLE_EXP, 5 << TABLE_EXP, 0, 0, 1 << TABLE_EXP, 5 << TABLE_EXP, 0, 0,
			0, 0, dpvm->any, dpvm->any, 0, 0);
	if (!type)
		return -2;

	m->mbox = dpvm_create_object(0, type, 1 << TABLE_EXP, 5 << TABLE_EXP, 0, 0);
	if (!m->mbox)
		return -3;

	m->empty = m->mbox->links[0];
	if (!dpvm_object_hash(0, m->empty, -4ull))
		return -4;

	for (i = 0; i < 1 << COND_EXP; ++i) {
		pthread_mutex_init(&m->mutex[i], NULL);
		pthread_cond_init(&m->cond[i], NULL);
	}
	
	return 0;
}

static int find_box(struct dpvm_mailbox *m, struct dpvm_object *thread, struct dpvm_object *address, int64_t taskid, int lock) {
	struct dpvm_hash *hash = dpvm_object_hash(thread, address, -3ull);
	int64_t *bucket, *taskids;
	int nbucket, i, j, k = -DPVM_ERROR_NO_MEMORY;

	if (!hash) return -DPVM_ERROR_NOT_FINISHED;
	
	nbucket = hash->hash[0] & (1 << (TABLE_EXP - BUCKET_EXP)) - 1;
	bucket = m->mbox->ints + (nbucket << (2 + BUCKET_EXP));
	taskids = m->mbox->ints + (4 << TABLE_EXP | nbucket << BUCKET_EXP);

	if (lock) pthread_mutex_lock(&m->mutex[nbucket]);

	for (i = 0; i < 1 << BUCKET_EXP; ++i) {
		if (taskids[i]) {
			for (j = 0; j < 4; ++j)
				if (hash->hash[j] != bucket[i << 2 | j])
					break;
			if (j == 4) {
				if (taskid != -1l) {
					if (taskids[i] != taskid) {
						pthread_mutex_unlock(&m->mutex[nbucket]);
						return -DPVM_ERROR_NOT_CHECKED;
					}
				}
				return nbucket << BUCKET_EXP | i;
			}
		} else if (k < 0) k = i;
	}
	if (k >= 0) {
		if (taskid != -1l) {
			for (j = 0; j < 4; ++j)
				bucket[k << 2 | j] = hash->hash[j];
			taskids[k] = taskid;
			__sync_add_and_fetch(&m->naddrs, 1);
			k |= nbucket << BUCKET_EXP;
		} else
			k = -DPVM_ERROR_NOT_CHECKED;
	}
	
	if (k < 0)
		pthread_mutex_unlock(&m->mutex[nbucket]);

	return k;
}

int dpvm_mailbox_send(struct dpvm_object *thread, struct dpvm_object *address, struct dpvm_object *message) {
	struct dpvm_mailbox *m = address->dpvm->mailbox;
	int err = 0, box = find_box(m, thread, address, -1l, 1), n = box >> BUCKET_EXP;
	if (box < 0) return -box;

	if (m->mbox->links[box] == m->empty) {
		struct dpvm_object *obj = dpvm_create_object(thread, m->empty->type, 0, 0, 0, 0);
		if (!obj) { err = -DPVM_ERROR_CREATE_OBJECT; goto end; }
		if ((err = dpvm_set_link(thread, m->mbox, box, obj))) goto end;
	}
	err = dpvm_push_link(thread, m->mbox->links[box], message);
	__sync_add_and_fetch(&m->nmess, 1);
	pthread_cond_broadcast(&m->cond[n]);

end:
	pthread_mutex_unlock(&m->mutex[n]);
	return err;
}

struct dpvm_object *dpvm_mailbox_receive(struct dpvm_object *thread, 
		struct dpvm_object *address, int64_t timeout, int64_t *status) {
	struct dpvm_mailbox *m = address->dpvm->mailbox;
	struct timespec ts;
	int64_t t, last = timeout + dpvm_get_time();
	int lock;
	*status = 0;
	for(lock = 1;;lock = 0) {
		int box = find_box(m, thread, address,
			thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_ID],
			lock), n = box >> BUCKET_EXP;
		if (box < 0) {
			*status = -box;
			break;
		}

		if (m->mbox->links[box] != m->empty) {
			struct dpvm_object *obj = m->mbox->links[box];
			int err = dpvm_set_link(thread, m->mbox, box, m->empty);
			if (timeout == 1ll << 63) {
				m->mbox->ints[4 << TABLE_EXP | box] = 0;
				__sync_add_and_fetch(&m->naddrs, -1ll);
			}
			__sync_add_and_fetch(&m->nmess, -obj->nlinks);
			pthread_mutex_unlock(&m->mutex[n]);
			return obj;
		}
		if (timeout == 1ll << 63) {
			m->mbox->ints[4 << TABLE_EXP | box] = 0;
			__sync_add_and_fetch(&m->naddrs, -1ll);
			pthread_mutex_unlock(&m->mutex[n]);
			break;
		}
		t = dpvm_get_time();
		if ((timeout >= 0 && t >= last) || thread->links[DPVM_THREAD_LINK_TASK]
				->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH) {
			pthread_mutex_unlock(&m->mutex[n]);
			break;
		}
		t += 100000000;
		if (timeout >= 0 && t > last)
			t = last;
		
		ts.tv_sec  = t / 1000000000;
		ts.tv_nsec = t % 1000000000;
		pthread_cond_timedwait(&m->cond[n], &m->mutex[n], &ts);

		t = dpvm_get_time();
		if ((timeout >= 0 && t >= last) || thread->links[DPVM_THREAD_LINK_TASK]
				->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH) {
			pthread_mutex_unlock(&m->mutex[n]);
			break;
		}
	}

	__sync_add_and_fetch(&m->empty->nrefs, 1);
	return m->empty;
}

void dpvm_mailbox_clear_task(struct dpvm_object *task) {
	struct dpvm_mailbox *m = task->dpvm->mailbox;
	int64_t id = task->ints[DPVM_TASK_INT_ID], *taskids = m->mbox->ints + (4 << TABLE_EXP);
	int i, n;

	for (n = 0; n < 1 << COND_EXP; ++n) {
		pthread_mutex_lock(&m->mutex[n]);
		for (i = 0; i < 1 << BUCKET_EXP; ++i) {
			int box = n << BUCKET_EXP | i;
			if (taskids[box] == id) {
				taskids[box] = 0;
				__sync_add_and_fetch(&m->naddrs, -1l);
				if (m->mbox->links[box] != m->empty) {
					__sync_add_and_fetch(&m->nmess, -m->mbox->links[box]->nlinks);
					dpvm_set_link(task, m->mbox, box, m->empty);
				}
			}
		}
		pthread_mutex_unlock(&m->mutex[n]);
	}
}

int64_t dpvm_mailbox_stats(struct dpvm *dpvm, int64_t n_getsys_param) {
	struct dpvm_mailbox *m = dpvm->mailbox;
	switch (n_getsys_param) {
		case DPVM_SYS_PARAM_NADDRS: return m->naddrs;
		case DPVM_SYS_PARAM_NMESS: return m->nmess;
	}
	return 0;
}
