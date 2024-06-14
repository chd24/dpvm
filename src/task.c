/* dpvm: task; T16.620-T19.703; $DVS:time$ */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "config.h"
#include "error.h"
#include "io.h"
#include "mailbox.h"
#include "object.h"
#include "task.h"
#include "thread.h"

struct dpvm_task {
	struct dpvm_object *type_task;
	pthread_mutex_t mutex;
	uint64_t id;
};

int dpvm_task_init(struct dpvm *dpvm) {
	struct dpvm_object *objs[3] = {dpvm->any, dpvm->any, dpvm->any};
	struct dpvm_task *t;

	dpvm->tasks = t = calloc(sizeof(struct dpvm_task), 1);
	if (!t)
		return -1;

	t->type_task = dpvm_create_type(0, DPVM_TASK_LINK_END, DPVM_TASK_INT_END, 0, 0,
			DPVM_TASK_LINK_END, DPVM_TASK_INT_END, 0, 0, 0, 0, 0, 0, 3, objs);
	if (!t->type_task)
		return -2;

	pthread_mutex_init(&t->mutex, NULL);
	
	t->id = 1;

	return 0;
}

static int64_t task_create(struct dpvm_object *parent, struct dpvm_object *thread) {
	struct dpvm_object *task = dpvm_create_object(parent, thread->dpvm->tasks->type_task, DPVM_TASK_LINK_END,
			DPVM_TASK_INT_END, 0, 0);
	int64_t err;
	if (!task)
		return DPVM_ERROR_CREATE_OBJECT;
	task->ints[DPVM_TASK_INT_START_TIME] = dpvm_get_time();
	task->ints[DPVM_TASK_INT_ID] = thread->dpvm->tasks->id++;
	if ((err = dpvm_set_link(parent, task, DPVM_TASK_LINK_HANDLER, task))) goto end;
	if (thread->links[DPVM_THREAD_LINK_TASK] != thread) {
		struct dpvm_object *ptask = thread->links[DPVM_THREAD_LINK_TASK];
		task->ints[DPVM_TASK_INT_FLAGS] = ptask->ints[DPVM_TASK_INT_FLAGS];
		task->ints[DPVM_TASK_INT_MEMORY_QUOTA] = ptask->ints[DPVM_TASK_INT_MEMORY_QUOTA];
		if ((err = dpvm_set_link(parent, task, DPVM_TASK_LINK_CHECKER, ptask->links[DPVM_TASK_LINK_CHECKER])))
			goto end;
		if ((err = dpvm_set_link(parent, task, DPVM_TASK_LINK_TRANSLATOR, ptask->links[DPVM_TASK_LINK_TRANSLATOR])))
			goto end;
		if (ptask->links[DPVM_TASK_LINK_CHILD] != ptask
				&& (err = dpvm_set_link(parent, task, DPVM_TASK_LINK_NEXT, ptask->links[DPVM_TASK_LINK_CHILD])))
			goto end;
		if ((err = dpvm_set_link(parent, task, DPVM_TASK_LINK_PARENT, ptask))) goto end;
		if ((err = dpvm_set_link(parent, ptask, DPVM_TASK_LINK_CHILD, task))) goto end;
		ptask->ints[DPVM_TASK_INT_N_CHILDS]++;
	} else {
		task->ints[DPVM_TASK_INT_MEMORY_QUOTA] = INT64_MAX;
		if (thread->dpvm->checker && (err = dpvm_set_link(parent, task, DPVM_TASK_LINK_CHECKER, thread->dpvm->checker)))
			goto end;
		if (thread->dpvm->translator && (err = dpvm_set_link(parent, task, DPVM_TASK_LINK_TRANSLATOR, thread->dpvm->translator)))
			goto end;
	}
	err = dpvm_set_link(parent, thread, DPVM_THREAD_LINK_TASK, task);
end:
	dpvm_unlink_object(parent, task);
	return err;
}

int64_t dpvm_task_create(struct dpvm_object *thread) {
	int64_t res;
	pthread_mutex_lock(&thread->dpvm->tasks->mutex);
	res = task_create(0, thread);
	pthread_mutex_unlock(&thread->dpvm->tasks->mutex);
	return res;
}

static struct dpvm_object *find_task_recurse(struct dpvm_object *task, int64_t *id, uint64_t flags, int gonext) {
	struct dpvm_object *ntask;

	do {	
		if (*id < 0) --*id;
		else if (*id == task->ints[DPVM_TASK_INT_ID]) return task;

		if (flags) {
			uint64_t f = flags & ~(uint64_t)(DPVM_TASK_FLAG_AND_RECURSIVE | DPVM_TASK_FLAG_OR_RECURSIVE);
			if (flags & DPVM_TASK_FLAG_AND_RECURSIVE)
				task->ints[DPVM_TASK_INT_FLAGS] &= f;
			if (flags & DPVM_TASK_FLAG_OR_RECURSIVE)
				task->ints[DPVM_TASK_INT_FLAGS] |= f;
		}

		ntask = task->links[DPVM_TASK_LINK_CHILD];
		while (ntask != task) {
			if (ntask->nrefs == 1 && ntask->links[DPVM_TASK_LINK_CHILD] == ntask) {
				dpvm_mailbox_clear_task(ntask);
				task->ints[DPVM_TASK_INT_N_CHILDS]--;
				if (ntask->links[DPVM_TASK_LINK_NEXT] == ntask)
					ntask = task;
				else
					ntask = ntask->links[DPVM_TASK_LINK_NEXT];
				if (dpvm_set_link(task, task, DPVM_TASK_LINK_CHILD, ntask))
					return 0;
			} else {
				ntask = find_task_recurse(ntask, id, flags, 1);
				if (ntask) return ntask;
				break;
			}
		}
		if (!gonext) break;

		ntask = task->links[DPVM_TASK_LINK_NEXT];
		while (ntask != task) {
			if (ntask->nrefs == 1 && ntask->links[DPVM_TASK_LINK_CHILD] == ntask) {
				dpvm_mailbox_clear_task(ntask);
				ntask->links[DPVM_TASK_LINK_PARENT]->ints[DPVM_TASK_INT_N_CHILDS]--;
				if (ntask->links[DPVM_TASK_LINK_NEXT] == ntask)
					ntask = task;
				else
					ntask = ntask->links[DPVM_TASK_LINK_NEXT];
				if (dpvm_set_link(ntask->links[DPVM_TASK_LINK_PARENT], task, DPVM_TASK_LINK_NEXT, ntask))
					return 0;
			} else {
				task = ntask;
				ntask = 0;
				break;
			}
		}
	} while (ntask != task);

	return 0;
}

static struct dpvm_object *find_task(struct dpvm_object *task, int64_t id) {
	struct dpvm_task *tasks = task->dpvm->tasks;	
	pthread_mutex_lock(&tasks->mutex);

	if (!id) return task;
	task = find_task_recurse(task, &id, 0, 0);
	if (!task)
		pthread_mutex_unlock(&tasks->mutex);

	return task;	
}

static int64_t task_getsys(struct dpvm_object *self, struct dpvm_object *task, int64_t param) {
	param = param & 0xffff;

	if (param >= DPVM_SYS_PARAM_NAME && param < DPVM_SYS_PARAM_NAME_END)
		param = task->ints[DPVM_TASK_INT_NAME + param - DPVM_SYS_PARAM_NAME];

	else if (param >= DPVM_SYS_PARAM_CHECKER_HASH && param < DPVM_SYS_PARAM_HASHES_END)
		param = task->ints[DPVM_TASK_INT_CHECKER_HASH + param - DPVM_SYS_PARAM_CHECKER_HASH];

	else switch (param) {
		case DPVM_SYS_PARAM_TIME:
			param = dpvm_get_time() - task->ints[DPVM_TASK_INT_START_TIME];
			break;
		case DPVM_SYS_PARAM_NTHREADS:
			param = task->nrefs - task->ints[DPVM_TASK_INT_N_CHILDS]
					- (task->links[DPVM_TASK_LINK_PARENT] != task);
			break;
		case DPVM_SYS_PARAM_MEMORY:
			param = task->ints[DPVM_TASK_INT_MEMORY];
			break;
		case DPVM_SYS_PARAM_FLAGS:
			param = task->ints[DPVM_TASK_INT_FLAGS];
			break;
		case DPVM_SYS_PARAM_ID:
			param = task->ints[DPVM_TASK_INT_ID];
			break;
		case DPVM_SYS_PARAM_ID_CHILD:
			param = -1ll;
			find_task_recurse(task, &param, 0, 0);
			if (task == task->links[DPVM_TASK_LINK_CHILD])
				param = -1ll;
			else
				param = task->links[DPVM_TASK_LINK_CHILD]->ints[DPVM_TASK_INT_ID];
			break;
		case DPVM_SYS_PARAM_ID_NEXT:
			param = -1ll;
			if (task != self) {
				find_task_recurse(task, &param, 0, 1);
				if (task == task->links[DPVM_TASK_LINK_NEXT])
					param = -1ll;
				else
					param = task->links[DPVM_TASK_LINK_NEXT]->ints[DPVM_TASK_INT_ID];
			}
			break;
		case DPVM_SYS_PARAM_NTASKS:
			param = -1ll;
			find_task_recurse(task, &param, 0, 0);
			param = ~param;
			break;
		case DPVM_SYS_PARAM_MEMORY_QUOTA:
			param = task->ints[DPVM_TASK_INT_MEMORY_QUOTA];
			break;
		default:
			param = -1ll;
			break;
	}

	return param;
}

int64_t dpvm_task_getsys(struct dpvm_object *thread, int64_t param) {
	struct dpvm_object *self = thread->links[DPVM_THREAD_LINK_TASK], *task = find_task(self, (uint64_t)param >> 16);
	if (!task) return -1ll;

	param = task_getsys(self, task, param);

	pthread_mutex_unlock(&task->dpvm->tasks->mutex);
	return param;
}

int64_t dpvm_task_setsys(struct dpvm_object *thread, int64_t param, int64_t value) {
	struct dpvm_object *self = thread->links[DPVM_THREAD_LINK_TASK], *task = find_task(self, (uint64_t)param >> 16);
	int64_t flags;
	int done = 0;
	if (!task) return -1ll;

	param &= 0xffff;
	flags = self->ints[DPVM_TASK_INT_FLAGS];

	if (param >= DPVM_SYS_PARAM_NAME && param < DPVM_SYS_PARAM_NAME_END) {
		if (!(flags & DPVM_TASK_FLAG_NO_NAME))
			task->ints[DPVM_TASK_INT_NAME + param - DPVM_SYS_PARAM_NAME] = value, done = 1;

	} else if (param >= DPVM_SYS_PARAM_CHECKER_HASH && param < DPVM_SYS_PARAM_HASHES_END) {
		if (self->links[DPVM_TASK_LINK_PARENT] == self) {
			task->ints[DPVM_TASK_INT_CHECKER_HASH + param - DPVM_SYS_PARAM_CHECKER_HASH] = value;
			if (param != DPVM_SYS_PARAM_CHECKER_HASH && param != DPVM_SYS_PARAM_TRANSLATOR_HASH)
				done = 1;
		}
	}

	if (!done) switch (param) {
		case DPVM_SYS_PARAM_FLAGS:
			if (flags & DPVM_TASK_FLAG_NO_FLAGS)
				value = task->ints[DPVM_TASK_INT_FLAGS];
			else if (value & (DPVM_TASK_FLAG_AND_RECURSIVE | DPVM_TASK_FLAG_OR_RECURSIVE))
				param = -1ll,
				find_task_recurse(task, &param, value, 0),
				value = task->ints[DPVM_TASK_INT_FLAGS];
			else
				task->ints[DPVM_TASK_INT_FLAGS] = value;
			break;
		case DPVM_SYS_PARAM_ID:
			if (flags & DPVM_TASK_FLAG_NO_CHILDS || task != self || task_create(thread, thread))
				value = -1ll;
			else
				value = thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_ID]; /* self changed */
			break;
		case DPVM_SYS_PARAM_MEMORY_QUOTA:
			if (flags & DPVM_TASK_FLAG_NO_SELF_MEMORY_QUOTA && task == self)
				value = task->ints[DPVM_TASK_INT_MEMORY_QUOTA];
			else
				task->ints[DPVM_TASK_INT_MEMORY_QUOTA] = value;
			break;
		case DPVM_SYS_PARAM_CHECKER_HASH:
			{
				struct dpvm_hash hash;
				struct dpvm_object *obj;
				memcpy(&hash, &task->ints[DPVM_TASK_INT_CHECKER_HASH], DPVM_HASH_SIZE);
				obj = dpvm_config_update(thread, &hash, "checker");
				if (!obj || dpvm_set_link(thread, task, DPVM_TASK_LINK_CHECKER, obj))
					value = (value == -1ll ? -2ll : -1ll);
				if (obj)
					dpvm_unlink_object(thread, obj);
			}
			break;
		case DPVM_SYS_PARAM_TRANSLATOR_HASH:
			{
				struct dpvm_hash hash;
				struct dpvm_object *obj;
				memcpy(&hash, &task->ints[DPVM_TASK_INT_TRANSLATOR_HASH], DPVM_HASH_SIZE);
				obj = dpvm_config_update(thread, &hash, "translator");
				if (!obj || dpvm_set_link(thread, task, DPVM_TASK_LINK_TRANSLATOR, obj))
					value = (value == -1ll ? -2ll : -1ll);
				if (obj)
					dpvm_unlink_object(thread, obj);
			}
			break;
		default:
			value = task_getsys(self, task, param);
			break;
	}	

	pthread_mutex_unlock(&task->dpvm->tasks->mutex);
	return value;
}
