/* dpvm: config; T15.430-T19.697; $DVS:time$ */

#ifndef DPVM_CONFIG
#define DPVM_CONFIG

#include "init.h"

struct dpvm_object;
struct dpvm_hash;

extern int dpvm_config_init(struct dpvm *dpvm);
extern struct dpvm_object *dpvm_config_update(struct dpvm_object *thread, struct dpvm_hash *hash, const char *itemName);

#endif

