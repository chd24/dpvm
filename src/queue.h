/* dpvm: queue; T17.903-T17.903; $DVS:time$ */

#ifndef DPVM_QUEUE
#define DPVM_QUEUE

#include <stdint.h>

struct dpvm_object;
struct dpvm_queue;

extern int dpvm_queue_init(struct dpvm_queue **pqueue);
extern int dpvm_queue_push(struct dpvm_queue *q, struct dpvm_object *obj);
extern int dpvm_queue_pop(struct dpvm_queue *q, struct dpvm_object **pobj);
extern int64_t dpvm_queue_get_size(struct dpvm_queue *q);
extern int64_t dpvm_queue_get_stall_time(struct dpvm_queue *q);
extern int dpvm_queue_release_finished_tasks(struct dpvm_queue *q);

#endif
