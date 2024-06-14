/* dpvm: thread; T15.530-T20.152; $DVS:time$ */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "bytecode.h"
#include "cache.h"
#include "error.h"
#include "hash.h"
#include "io.h"
#include "malloc.h"
#include "object.h"
#include "queue.h"
#include "run.h"
#include "task.h"
#include "thread.h"
#include "transaction.h"

#define N_MAX_PTHREADS              0x100
#define N_STEPS                     0x10000
#define WORKER_CREATE_PERIOD_MS     100
#define RELEASE_FINISHED_TASKS_MS   1000
#define CHILD_KILL_TIMEOUT          20

struct dpvm_thread {
        pthread_t                   thr[N_MAX_PTHREADS + 1];
        struct dpvm_object *        type_stack;
        struct dpvm_object *        type_thread;
        struct dpvm_object *        empty_stack;
        struct dpvm_queue *         queue;
        struct dpvm_transaction *   transaction;
        int                         nthreads;
        int                         nactive;
        int                         npthreads;
};

int dpvm_thread_finished(struct dpvm_object *thread) {
	return thread->ints[DPVM_THREAD_INT_FLAGS] & DPVM_THREAD_FLAG_FINISH;
}

int dpvm_thread_finish(struct dpvm_object *thread) {
	if (dpvm_thread_finished(thread)) return DPVM_ERROR_FINISHED;
	thread->ints[DPVM_THREAD_INT_FLAGS] |= DPVM_THREAD_FLAG_FINISH;
	__sync_fetch_and_add(&thread->dpvm->threads->nactive, -1);
	return 0;
}

int dpvm_thread_prepare_result(struct dpvm_object *thread, struct dpvm_object *result_type) {
	struct dpvm_thread *t = thread->dpvm->threads;
	int64_t nlinks = result_type->ints[DPVM_TYPE_N_LINKS_MIN],
		nints = result_type->ints[DPVM_TYPE_N_INTS_MIN],
		nfloats = result_type->ints[DPVM_TYPE_N_FLOATS_MIN],
		ncodes = result_type->ints[DPVM_TYPE_N_CODES_MIN],
		i;

	struct dpvm_object *stack = thread->links[DPVM_THREAD_LINK_STACK],
		*out = dpvm_create_object(thread, result_type, nlinks, nints, nfloats, ncodes);
	if (!out) return DPVM_ERROR_CREATE_OBJECT;

	for (i = 0; i < nlinks; ++i)
		dpvm_set_link(thread, out, i, stack->links[i]);
	if (nints) memcpy(out->ints, stack->ints, nints * sizeof(int64_t));
	if (nfloats) memcpy(out->floats, stack->floats, nfloats * sizeof(double));
	if (ncodes) memcpy(out->codes, stack->codes, ncodes);

	dpvm_set_link(thread, thread, DPVM_THREAD_LINK_RESULT, out);
	dpvm_set_link(thread, thread, DPVM_THREAD_LINK_STACK, t->empty_stack);

	dpvm_unlink_object(thread, out);
	return DPVM_ERROR_FINISHED;
}

void dpvm_thread_unlink(struct dpvm_object *thread) {
	if (!__sync_add_and_fetch(&thread->nrefs, -1)) {
		struct dpvm_object *parent = thread->links[DPVM_THREAD_LINK_PARENT],
				*task = thread->links[DPVM_THREAD_LINK_TASK];
		int64_t in = thread->ints[DPVM_THREAD_INT_INPUT],
		       out = thread->ints[DPVM_THREAD_INT_OUTPUT];
		pid_t  pid = thread->ints[DPVM_THREAD_INT_CHILD];
		int status;

		if (!dpvm_thread_finished(thread))
			dpvm_thread_finish(thread);

                if (thread->ints[DPVM_THREAD_INT_FLAGS] & DPVM_THREAD_FLAG_RUN) {
			dpvm_link_object(thread->links[DPVM_THREAD_LINK_DATA]);
			dpvm_link_object(thread->links[DPVM_THREAD_LINK_RESULT]);
			dpvm_transaction_set_result(thread->dpvm->threads->transaction, thread,
                                thread->links[DPVM_THREAD_LINK_DATA],
                                thread->links[DPVM_THREAD_LINK_RESULT], DPVM_ERROR_TERMINATED);
                }

		if (in > fileno(stderr) && (thread == parent ||
				in != parent->ints[DPVM_THREAD_INT_INPUT]))
			close(in);
		if (out != in && out > fileno(stderr) && (thread == parent ||
				out != parent->ints[DPVM_THREAD_INT_OUTPUT]))
			close(out);

		if (pid) {
			time_t t = time(0);
			int killed = 0;
			kill(pid, SIGTERM);
			while (time(0) - t < CHILD_KILL_TIMEOUT) {
				if (task->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH) break;
				if (waitpid(pid, &status, WNOHANG))
					{ killed = 1; break; }
				usleep(100000);
			}
			if (!killed) {
				kill(pid, SIGKILL);
				waitpid(pid, &status, 0);
			}
		}

		if (thread != parent) {
			dpvm_thread_unlink(parent);
			thread->links[DPVM_THREAD_LINK_PARENT] = thread;
		}

		__sync_add_and_fetch(&thread->dpvm->threads->nthreads, -1);
		if (task != thread)
			dpvm_link_object(task);
		dpvm_free_object(task, thread);
		if (task != thread)
			dpvm_unlink_object(task, task);
	}
}

int dpvm_thread_get_result(struct dpvm_object **pthread) {
	struct dpvm_object *thread = *pthread, *out;
	if (!dpvm_thread_finished(thread)) return DPVM_ERROR_NOT_FINISHED;
	out = thread->links[DPVM_THREAD_LINK_RESULT];
        dpvm_link_object(out);
	dpvm_thread_unlink(thread);
	*pthread = out;
	return 0;
}

static int dpvm_thread_handle_err(struct dpvm_object *thread, int64_t err) {
	struct dpvm_object *task, *template, *handler;
	task = thread->links[DPVM_THREAD_LINK_TASK];
        thread->ints[DPVM_THREAD_INT_FLAGS] |= DPVM_THREAD_FLAG_ERROR;
        thread->ints[DPVM_THREAD_INT_ERROR] = err;
        if ((err & 31) == DPVM_ERROR_TERMINATED)
            return 0;
	template = task->links[DPVM_TASK_LINK_HANDLER];
	if (template == task) return -5;
	handler = dpvm_thread_create(template->links[DPVM_THREAD_LINK_FUNC],
		template->links[DPVM_THREAD_LINK_ARG], 0,
		template->links[DPVM_THREAD_LINK_DATA], thread,
		0, 0, 0, 0, 0, err);
	if (!handler) return -6;
	return dpvm_thread_io(handler, template->ints[DPVM_THREAD_INT_FLAGS] 
		/ DPVM_THREAD_FLAG_IO_CODE & 0xff);
}

static void *worker_thread_run(void *arg) {
	struct dpvm *dpvm = (struct dpvm *)arg;
	struct dpvm_thread *t = dpvm->threads;
        struct dpvm_object *thread;
        int64_t err;

        while (!dpvm_queue_pop(t->queue, &thread)) {
                if (!dpvm_thread_finished(thread)) {
                        err = dpvm_run_thread(thread, N_STEPS);
                        if (thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH)
                            err = DPVM_ERROR_TERMINATED;
                        switch (err) {
				case DPVM_ERROR_SUCCESS:
				case DPVM_ERROR_FINISHED:
					break;
				default:
					dpvm_thread_handle_err(thread, err);
					dpvm_thread_finish(thread);
					break;
                        }
                }
                if (dpvm_thread_finished(thread)) {
                        if (thread->ints[DPVM_THREAD_INT_FLAGS] & DPVM_THREAD_FLAG_RUN) {
				struct dpvm_object *transaction = thread->links[DPVM_THREAD_LINK_DATA],
						*result = thread->links[DPVM_THREAD_LINK_RESULT];
				dpvm_link_object(transaction);
				dpvm_link_object(result);

				if (thread->ints[DPVM_THREAD_INT_FLAGS] & DPVM_THREAD_FLAG_ERROR)
					err = thread->ints[DPVM_THREAD_INT_ERROR];
                                else {
					struct dpvm_object *empty = thread->links[DPVM_THREAD_LINK_NAME];
					err = dpvm_set_link(thread, thread, DPVM_THREAD_LINK_RESULT, empty)
						| dpvm_set_link(thread, thread, DPVM_THREAD_LINK_DATA, empty)
						| dpvm_set_link(thread, thread, DPVM_THREAD_LINK_ARG, empty)
						| dpvm_set_link(thread, thread, DPVM_THREAD_LINK_FUNC, empty);
                                }

				dpvm_transaction_set_result(t->transaction, thread, transaction, result, err);
				thread->ints[DPVM_THREAD_INT_FLAGS] &= ~DPVM_THREAD_FLAG_RUN;
                        }
                        dpvm_thread_unlink(thread);
                } else if (dpvm_queue_push(t->queue, thread))
                        dpvm_thread_unlink(thread);
	}

        return 0;
}

static void *monitor_thread_run(void *arg) {
	struct dpvm *dpvm = (struct dpvm *)arg;
	struct dpvm_thread *t = dpvm->threads;
        int i, max = RELEASE_FINISHED_TASKS_MS / WORKER_CREATE_PERIOD_MS;

        for(i = 0;;++i, i %= max) {
                if (!t->npthreads || t->npthreads < N_MAX_PTHREADS && dpvm_queue_get_stall_time(t->queue) >= WORKER_CREATE_PERIOD_MS * 1000000l) {
                        t->npthreads++;
                        if (pthread_create(&t->thr[t->npthreads], 0, worker_thread_run, dpvm))
                                t->npthreads--;
		}
                usleep(WORKER_CREATE_PERIOD_MS * 1000);
                if (!i)
                        dpvm_queue_release_finished_tasks(t->queue);
	}	

        return 0;
}

int dpvm_thread_init(struct dpvm *dpvm) {
	struct dpvm_object *objs[DPVM_THREAD_LINK_END];
	struct dpvm_thread *t;
	int i, err;

	dpvm->threads = t = calloc(sizeof(struct dpvm_thread), 1);
	if (!t)
		return -1;

        if ((err = dpvm_queue_init(&t->queue)))
                return err * 10 - 2;

	t->type_stack = dpvm_create_type(0, 0, 0, 0, 0,
			INT64_MAX, INT64_MAX, INT64_MAX, INT64_MAX,
			dpvm->any, dpvm->any, dpvm->any, dpvm->any, 0, 0);
	if (!t->type_stack)
                return -4;

	t->empty_stack = dpvm_create_object(0, t->type_stack, 0, 0, 0, 0);
	if (!t->empty_stack)
                return -5;

	for (i = 0; i < DPVM_THREAD_LINK_END; ++i)
		objs[i] = dpvm->any;
	objs[DPVM_THREAD_LINK_PARENT] = 0;
	objs[DPVM_THREAD_LINK_STACK] = t->type_stack;

	t->type_thread = dpvm_create_type(0,
			DPVM_THREAD_LINK_END, DPVM_THREAD_INT_END, 0, 0,
			INT64_MAX, INT64_MAX, 0, 0,
			dpvm->any, dpvm->any, dpvm->any, dpvm->any, 
			DPVM_THREAD_LINK_END, objs);
	if (!t->type_thread)
                return -6;

        if (pthread_create(&t->thr[0], 0, monitor_thread_run, dpvm))
                return -7;

	return 0;
}

int dpvm_thread_post_init(struct dpvm *dpvm) {
	struct dpvm_thread *t = dpvm->threads;
	int err;

        if ((err = dpvm_transaction_init(dpvm, &t->transaction)))
                return err > 0 ? -(err << 4 | 1) : -(-err << 4 | 2);

	if ((err = dpvm_cache_add(dpvm, 0, &t->type_stack)))
		return -(err << 4 | 3);
	if ((err = dpvm_cache_add(dpvm, 0, &t->empty_stack)))
		return -(err << 4 | 4);
	if ((err = dpvm_cache_add(dpvm, 0, &t->type_thread)))
		return -(err << 4 | 5);

	return 0;
}

#define IN(t)		t->ints[DPVM_THREAD_INT_INPUT]
#define OUT(t)		t->ints[DPVM_THREAD_INT_OUTPUT]
#define PARENT(t)	t->links[DPVM_THREAD_LINK_PARENT]

static int dpvm_thread_correct_parent(struct dpvm_object *thread) {
	struct dpvm_object *parent = PARENT(thread);
	int i, o, err;
	if (parent == thread) return 0;
	i = ( IN(parent) !=  IN(thread));
	o = (OUT(parent) != OUT(thread));
	if (i && o) {
		__sync_add_and_fetch(&parent->nrefs, 1);
		err = dpvm_set_link(thread, thread, DPVM_THREAD_LINK_PARENT, thread);
		dpvm_thread_unlink(parent);
		return err;
	}
	while (PARENT(parent) != parent
			&& (i ||  IN(PARENT(parent)) ==  IN(parent))
			&& (o || OUT(PARENT(parent)) == OUT(parent))) {
		__sync_add_and_fetch(&parent->nrefs, 1);
		err = dpvm_set_link(thread, thread, DPVM_THREAD_LINK_PARENT, PARENT(parent));
		dpvm_thread_unlink(parent);
		if (err) return err;
		parent = PARENT(thread);
	}
	return 0;
}

#undef IN
#undef OUT
#undef PARENT

struct dpvm_object *dpvm_thread_create(struct dpvm_object *func, struct dpvm_object *arg,  struct dpvm_object *name, 
		struct dpvm_object *data, struct dpvm_object *parent,
		int flags, int in, int out, size_t pos, uint64_t size, uint64_t ext) {
        struct dpvm_object *thread = dpvm_create_object(parent, func->dpvm->threads->type_thread, DPVM_THREAD_LINK_END,
			DPVM_THREAD_INT_END + DPVM_THREAD_INT_EXT_END, 0, 0);
	if (!thread) return 0;
	if (parent) {
		if (dpvm_set_link(parent, thread, DPVM_THREAD_LINK_TASK, parent->links[DPVM_THREAD_LINK_TASK])) goto err;

		if (flags & DPVM_THREAD_FLAG_NO_PARENT) {
			parent = thread;
			flags &= ~DPVM_THREAD_FLAG_NO_PARENT;
		} else {
			flags |= parent->ints[DPVM_THREAD_INT_FLAGS ] & ~(DPVM_THREAD_FLAG_FINISH
			            | DPVM_THREAD_FLAG_RUN | DPVM_THREAD_FLAG_ERROR | DPVM_THREAD_FLAG_IO_CODE * 0xff);
			in     = parent->ints[DPVM_THREAD_INT_INPUT ];
			out    = parent->ints[DPVM_THREAD_INT_OUTPUT];
		}
	} else {
		if (dpvm_set_link(parent, thread, DPVM_THREAD_LINK_TASK, thread) || dpvm_task_create(thread))
			goto err;
		parent = thread;
	}
	if (dpvm_set_link(parent, thread, DPVM_THREAD_LINK_PARENT, parent)) goto err;
	if (func && dpvm_set_link(parent, thread, DPVM_THREAD_LINK_FUNC, func)) goto err;
	if (arg  && dpvm_set_link(parent, thread, DPVM_THREAD_LINK_ARG , arg )) goto err;
	if (name && dpvm_set_link(parent, thread, DPVM_THREAD_LINK_NAME, name)) goto err;
	if (data && dpvm_set_link(parent, thread, DPVM_THREAD_LINK_DATA, data)) goto err;
	thread->ints[DPVM_THREAD_INT_FLAGS ] = flags;
	thread->ints[DPVM_THREAD_INT_INPUT ] = in;
	thread->ints[DPVM_THREAD_INT_OUTPUT] = out;
	thread->ints[DPVM_THREAD_INT_CHILD ] = 0;
	thread->ints[DPVM_THREAD_INT_POS   ] = pos;
	thread->ints[DPVM_THREAD_INT_SIZE  ] = size;
	thread->ints[DPVM_THREAD_INT_EXT   ] = ext;
	if (dpvm_thread_correct_parent(thread)) goto err;
	__sync_fetch_and_add(&func->dpvm->threads->nthreads, 1);
	__sync_fetch_and_add(&func->dpvm->threads->nactive, 1);
	return thread;
err:
	dpvm_thread_unlink(thread);
	return 0;
}

int dpvm_thread_start(struct dpvm_object *thread, struct dpvm_object **links) {
	struct dpvm_object *arg = thread->links[DPVM_THREAD_LINK_ARG],
		*stack = dpvm_create_object(thread, thread->dpvm->threads->type_stack,
			arg->nlinks, arg->nints, arg->nfloats, arg->ncodes);
	int i, err = 0;
	if (!stack) return DPVM_ERROR_CREATE_OBJECT;
	if ((err = dpvm_set_link(thread, thread, DPVM_THREAD_LINK_STACK, stack)))
		return err;
	for (i = 0; i < arg->nlinks; ++i) {
		if (links) {
			struct dpvm_object *tmp = links[i];
			links[i] = stack->links[i];
			stack->links[i] = tmp;
		} else if (arg->links[i]->type == arg->links[i] && arg->links[i]->ints[15])
			return DPVM_ERROR_CONST;
		else if ((err = dpvm_set_link(thread, stack, i, arg->links[i])))
			return err;
	}
	if (arg->nints) memcpy(stack->ints, arg->ints, arg->nints * sizeof(int64_t));
	if (arg->nfloats) memcpy(stack->floats, arg->floats, arg->nfloats * sizeof(double));
	if (arg->ncodes) memcpy(stack->codes, arg->codes, arg->ncodes);
	thread->ints[DPVM_THREAD_INT_END + DPVM_THREAD_INT_EXT_NLINKS] = DPVM_THREAD_LINK_FUNC;
	dpvm_unlink_object(thread, stack);
	return err;
}

int dpvm_thread_run(struct dpvm_object *thread, struct dpvm_object **links) {
        int err;

        err = dpvm_thread_correct_parent(thread);
        if (err) return err;

        err = dpvm_thread_start(thread, links);
        if (err) return err;

        return dpvm_queue_push(thread->dpvm->threads->queue, thread);
}

static void do_io(struct dpvm_object *thread, int handle_err) {
	struct dpvm_object *tmp;
	int64_t err = 0;
	int i, code;

	code = (thread->ints[DPVM_THREAD_INT_FLAGS] / DPVM_THREAD_FLAG_IO_CODE) & 0xff;
	for (i = 0; (code & 0xEF) != dpvm_io_run_funcs[i].code; ++i)
		;

	tmp = (*dpvm_io_run_funcs[i].run)(thread);
	if (!tmp) err = DPVM_ERROR_IO;
	if (!err) err = dpvm_set_link(thread, tmp, 0, thread->links[DPVM_THREAD_LINK_ARG]);
	if (!err && code >= 0xF0 && !dpvm_object_hash(tmp, -4ull))
		err = DPVM_ERROR_NOT_FINISHED;
	if (!err) err = dpvm_set_link(thread, thread, DPVM_THREAD_LINK_ARG, tmp);
	if (tmp) dpvm_unlink_object(thread, tmp);
	if (!err) err = dpvm_thread_run(thread, NULL);
	if (err) {
		struct dpvm_hash *hash = dpvm_object_hash(thread->links[DPVM_THREAD_LINK_FUNC], -3ull);
		err &= 0x1f;
		err |= (code | 0xf00) << 5; 
		if (hash)
			err |= hash->hash[0] << 17;
		if (handle_err)
			dpvm_thread_handle_err(thread, err);
		dpvm_thread_unlink(thread);
	}
}

static void *io_run(void *arg) {
	pthread_detach(pthread_self());
	do_io((struct dpvm_object *)arg, 1);
	return 0;
}

#define int enum {
#define _Z 0};
#include "../dpvm/common/mpopenFlags.dpvmh"
#undef _Z
#undef int

int dpvm_thread_io(struct dpvm_object *thread, int code) {
	struct dpvm_object *obj;
	pthread_t t;
	int i, res = (code >= 0xF0 ? 0 : DPVM_ERROR_FINISHED);

	if (!res) {
		if ((obj = thread->links[DPVM_THREAD_LINK_FUNC])
			&& !dpvm_object_hash(obj, -4ull)) goto err;
		if ((obj = thread->links[DPVM_THREAD_LINK_ARG])
			&& !dpvm_object_hash(obj, -4ull)) goto err;
		if ((obj = thread->links[DPVM_THREAD_LINK_NAME])
			&& !dpvm_object_hash(obj, -4ull)) goto err;
		if ((obj = thread->links[DPVM_THREAD_LINK_DATA])
			&& !dpvm_object_hash(obj, -4ull)) goto err;
	} else if (code == DPVM_CODE_BIND || (code == DPVM_CODE_MPOPEN
            && thread->ints[DPVM_THREAD_INT_EXT] != DPVM_MPOPEN_READ
            && thread->ints[DPVM_THREAD_INT_EXT] != DPVM_MPOPEN_RW_MERGED)) {
		if ((obj = thread->links[DPVM_THREAD_LINK_ARG])
			&& !dpvm_object_hash(obj, -4ull)) goto err;
	}

	if (!(thread->ints[DPVM_THREAD_INT_FLAGS] & DPVM_THREAD_FLAG_IO))
		goto err;
	if (thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS]
			& (1ll << (code - 0xE0 + DPVM_TASK_FLAG_NO_IO_EXP) | DPVM_TASK_FLAG_FINISH))
		goto err;

	for (i = 0; ; ++i) {
		if (dpvm_io_run_funcs[i].code < 0)
			goto err;
		if ((code & 0xEF) == dpvm_io_run_funcs[i].code) break;
	}

	thread->ints[DPVM_THREAD_INT_FLAGS] &= ~(0xffll * DPVM_THREAD_FLAG_IO_CODE);
	thread->ints[DPVM_THREAD_INT_FLAGS] |= code * DPVM_THREAD_FLAG_IO_CODE;

	if ((code == DPVM_CODE_GETSYS || code == DPVM_CODE_GETSYSP) && !thread->ints[DPVM_THREAD_INT_EXT]) {
		struct dpvm_object *data = thread->links[DPVM_THREAD_LINK_DATA];
		for (i = 0; i < data->nints; ++i) {
			if (data->ints[i] >= DPVM_SYS_PARAM_ERROR && data->ints[i] < DPVM_SYS_PARAM_ERROR_END) {
				struct dpvm_object *task = thread->links[DPVM_THREAD_LINK_TASK];
				if ((obj = thread->links[DPVM_THREAD_LINK_ARG]) && !dpvm_object_hash(obj, -4ull)) goto err;
				if (task->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_NO_HANDLER) goto err;
				dpvm_set_link(thread, task, DPVM_TASK_LINK_HANDLER, thread);
				dpvm_set_link(thread, thread, DPVM_THREAD_LINK_TASK, thread);
				dpvm_set_link(thread, thread, DPVM_THREAD_LINK_PARENT, thread);
				__sync_add_and_fetch(&thread->dpvm->threads->nactive, -1);
				__sync_add_and_fetch(&thread->dpvm->threads->nthreads, -1);
				goto err;
			}
		}
	}

	if (!(obj = thread->links[DPVM_THREAD_LINK_FUNC]) || !obj->ncodes)
		do_io(thread, 0);
	else if (res)
		do_io(thread, 1);
	else if (pthread_create(&t, 0, &io_run, thread))
		goto err;
	return res;
err:
	dpvm_thread_unlink(thread);
	return res;
}

int dpvm_thread_run_transaction(struct dpvm_object *thread, struct dpvm_object *transaction, struct dpvm_object **links)
{
        struct dpvm_object *new_thread = dpvm_thread_create(transaction->links[0], transaction->links[1],
                0, transaction, thread, DPVM_THREAD_FLAG_RUN, 0, 0, 0, 0, 0);
        int err;

        if (!new_thread)
            return DPVM_ERROR_CREATE_OBJECT;

        err = dpvm_thread_run(new_thread, links);
        if (err) dpvm_thread_unlink(new_thread);
        return err;
}

int dpvm_thread_do_run(struct dpvm_object **links, int64_t *ints,
                       double *floats, uint8_t *codes, struct dpvm_object *thread)
{
        struct dpvm_object *func = links[-1], *intype = func->type->links[0], *arg, *transaction = 0;
        int64_t nlinks = intype->ints[8], nints = intype->ints[9],
                nfloats = intype->ints[10], ncodes = intype->ints[11], i;
        int err = 0;

        err = dpvm_transaction_build(thread->dpvm->threads->transaction, thread, func, links - 1 - nlinks,
			&transaction);
        if (err > 0)
		goto end;

        arg = transaction->links[1];

        if (nints) memcpy(arg->ints, ints - nints, nints * sizeof(int64_t));
        if (nfloats) memcpy(arg->floats, floats - nfloats, nfloats * sizeof(double));
        if (ncodes) memcpy(arg->codes, codes - ncodes, ncodes * sizeof(uint8_t));

        err = dpvm_transaction_add(thread->dpvm->threads->transaction, thread, transaction, err);
	if (err == DPVM_ERROR_FINISHED);
        else if (err)
		goto end;
        else
                err = dpvm_thread_run_transaction(thread, transaction, links - 1 - nlinks);

        if (!err || err == DPVM_ERROR_FINISHED) {
                for (i = 0; i <= nlinks; ++i)
                        dpvm_unlink_object(thread, links[-1 - i]);
                links[-1 - nlinks] = transaction;
                transaction = 0;
        }

end:
        if (transaction) dpvm_unlink_object(thread, transaction);

        if (err == DPVM_ERROR_FINISHED)
		err = 0;
        return err;
}

int dpvm_thread_do_wait(struct dpvm_object **links, int64_t *ints,
                        double *floats, uint8_t *codes, struct dpvm_object *thread)
{
        struct dpvm_thread *dt = thread->dpvm->threads;
        struct dpvm_object *transaction = links[-1], *func = transaction->links[0],
                    *outtype = func->type->links[DPVM_TYPE_OUT_ARGS], *res;
        int64_t nlinks = outtype->ints[8], nints = outtype->ints[9],
                nfloats = outtype->ints[10], ncodes = outtype->ints[11];
        int64_t err, i;

        err = dpvm_transaction_get_result(dt->transaction, thread, transaction, &res);
        if (err) return err;

        if (nlinks) memcpy(links - 1, res->links, nlinks * sizeof(struct dpvm_object *));
        for (i = 0; i < nlinks; ++i)
                dpvm_link_object(res->links[i]);
        if (nints) memcpy(ints, res->ints, nints * sizeof(int64_t));
        if (nfloats) memcpy(floats, res->floats, nfloats * sizeof(double));
        if (ncodes) memcpy(codes, res->codes, ncodes * sizeof(uint8_t));

        dpvm_unlink_object(thread, res);
        dpvm_unlink_object(thread, transaction);
        return 0;
}

int64_t dpvm_thread_stats(struct dpvm *dpvm, int64_t n_getsys_param) {
	switch (n_getsys_param) {
		case DPVM_SYS_PARAM_MEMORY: return dpvm->memory;
		case DPVM_SYS_PARAM_ALLOCED: return mallinfo2().arena;
		case DPVM_SYS_PARAM_MAPPED: return mallinfo2().hblkhd;
		case DPVM_SYS_PARAM_NOBJECTS: return dpvm->nobjects;
		case DPVM_SYS_PARAM_NTHREADS: return dpvm->threads->nthreads;
		case DPVM_SYS_PARAM_NACTIVE: return dpvm->threads->nactive;
		case DPVM_SYS_PARAM_NPTHREADS: return dpvm->threads->npthreads;
		case DPVM_SYS_PARAM_QSIZE: return dpvm_queue_get_size(dpvm->threads->queue);
		case DPVM_SYS_PARAM_QTIME: return dpvm_queue_get_stall_time(dpvm->threads->queue) / 1000000;
		case DPVM_SYS_PARAM_NTRANSACTIONS: return dpvm_transaction_get_nalloced(dpvm->threads->transaction);
		case DPVM_SYS_PARAM_NRESULTS: return dpvm_transaction_get_nready(dpvm->threads->transaction);
        }
	return 0;
}

void dpvm_thread_change_nactive(struct dpvm *dpvm, int diff) {
	__sync_fetch_and_add(&dpvm->threads->nactive, diff);
}

struct dpvm_object *dpvm_thread_type_transaction(struct dpvm *dpvm) {
        return dpvm_transaction_get_type(dpvm->threads->transaction);
}
