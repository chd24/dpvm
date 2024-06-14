/* dpvm: queue; T17.903-T19.630; $DVS:time$ */

#include <stdlib.h>
#include <pthread.h>
#include "error.h"
#include "io.h"
#include "object.h"
#include "task.h"
#include "thread.h"

#define QUEUE_NEXT  DPVM_THREAD_LINK_RESULT

struct dpvm_queue {
        pthread_cond_t          cond;
        pthread_mutex_t         mutex;
        struct dpvm_object *	head;
        struct dpvm_object *	tail;
        int64_t                 size;
        int64_t                 max_size;
        int64_t                 stall_time;
};

int dpvm_queue_init(struct dpvm_queue **pqueue) {
        struct dpvm_queue *q = calloc(sizeof(struct dpvm_queue), 1);
        if (!q) return -1;

        pthread_cond_init(&q->cond, 0);
        pthread_mutex_init(&q->mutex, 0);

        *pqueue = q;
        return 0;
}

int dpvm_queue_push(struct dpvm_queue *q, struct dpvm_object *obj) {
        int err = 0;

        if (obj->nlinks <= QUEUE_NEXT)
                return DPVM_ERROR_NOT_IMPLEMENTED;

        pthread_mutex_lock(&q->mutex);

        if (q->size) {
                if ((err = dpvm_set_link(obj, q->tail, QUEUE_NEXT, obj)))
                        goto end;
        } else {
                q->head = obj;
                q->stall_time = dpvm_get_time();
        }

        q->tail = obj;
        q->size++;
        if (q->size > q->max_size)
		q->max_size = q->size;
        pthread_cond_signal(&q->cond);

end:
        pthread_mutex_unlock(&q->mutex);

        return err;
}

int dpvm_queue_pop(struct dpvm_queue *q, struct dpvm_object **pobj) {
        int err = 0;

        pthread_mutex_lock(&q->mutex);

        while (!q->size)
		pthread_cond_wait(&q->cond, &q->mutex);

        if (q->size > 1) {
                struct dpvm_object *next = q->head->links[QUEUE_NEXT];
                if ((err = dpvm_set_link(q->head, q->head, QUEUE_NEXT, q->head)))
                        goto end;
                *pobj = q->head;
                q->head = next;
                /* q->stall_time = dpvm_get_time(); */
        } else {
                *pobj = q->head;
                q->head = q->tail = 0;
		q->stall_time = dpvm_get_time();
        }

        q->size--;

end:
        pthread_mutex_unlock(&q->mutex);

        return 0;
}

int64_t dpvm_queue_get_size(struct dpvm_queue *q) {
        int64_t size;

        pthread_mutex_lock(&q->mutex);

        size = q->max_size;

        pthread_mutex_unlock(&q->mutex);

        return size;
}

int64_t dpvm_queue_get_stall_time(struct dpvm_queue *q) {
        int64_t t;

        pthread_mutex_lock(&q->mutex);

        if (q->size)
		t = dpvm_get_time() - q->stall_time;
        else
		t = 0;

        pthread_mutex_unlock(&q->mutex);

        return t;
}

int dpvm_queue_release_finished_tasks(struct dpvm_queue *q) {
        struct dpvm_object *prev = 0, *cur, *next;
        int err = 0, err1;

        pthread_mutex_lock(&q->mutex);

        for (cur = q->head; cur; cur = next) {
                next = (cur == q->tail ? 0 : cur->links[QUEUE_NEXT]);

                if (cur->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH) {
                        if (prev) {
                                if (next) {
                                        err1 = dpvm_set_link(cur, prev, QUEUE_NEXT, next);
                                        if (err1) continue;
                                        err |= err1 | dpvm_set_link(cur, cur, QUEUE_NEXT, cur);
                                } else {
                                        err1 = dpvm_set_link(cur, prev, QUEUE_NEXT, prev);
                                        if (err1) continue;
                                        err |= err1;
                                        q->tail = prev;
                                }
                        } else {
                                if (next) {
                                        err |= dpvm_set_link(cur, cur, QUEUE_NEXT, cur);
                                        q->head = next;
                                } else {
                                        q->head = q->tail = 0;
                                }
                        }

                        q->size--;
                        dpvm_thread_unlink(cur);
                } else
                        prev = cur;
        }

        q->max_size = q->size;

        pthread_mutex_unlock(&q->mutex);

        return err;
}
