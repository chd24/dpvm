/* dpvm: config; T15.430-T20.174; $DVS:time$ */

#include <stdio.h>
#include <string.h>
#include "cache.h"
#include "config.h"
#include "name.h"
#include "object.h"
#include "run.h"

#define PROFILE ".dpvm_profile.txt"
#define LINE_MAX 128

static struct configItem {
	const char *name;
	struct dpvm_object **ptr;
	int nlink;
} items[] = {
	{ "checker",	&((struct dpvm *)0)->checker,	   -1 },
	{ "translator", &((struct dpvm *)0)->translator,    1 },
	{ "monitor",	&((struct dpvm *)0)->monitor,	    2 }
};

int dpvm_config_init(struct dpvm *dpvm) {
	FILE *f = fopen(PROFILE, "r");
	struct dpvm_object *obj;
	char buf[LINE_MAX], name[LINE_MAX], fmt[LINE_MAX];
	struct dpvm_hash hash;
	int64_t short_hash;
	int mask = 0, i;

	for (i = 0; i < sizeof(items) / sizeof(struct configItem); i++)
		*(unsigned long *)&items[i].ptr += (unsigned long)dpvm;

	if (!f) {
		printf("Error: can't open dpvm profile '%s'.\n", PROFILE);
		return -1;
	}

	while (fgets(buf, LINE_MAX, f)) {
		for (i = 0; i < sizeof(items) / sizeof(struct configItem); i++) {
			sprintf(fmt, " set %s = %%s", items[i].name);
			if (sscanf(buf, fmt, &name) == 1) {
				short_hash = dpvm_name2hash(dpvm, name, &hash);
				if (short_hash < 0) {
					printf("Error: dpvm %s object has illegal name '%s'.\n", items[i].name, name);
					fclose(f);
					return -2 - (i << 2);
				}
				if (short_hash >> 47)
					obj = dpvm_hash2object(dpvm, 0, &hash);
				else
					obj = dpvm_short_hash2object(dpvm, 0, short_hash);
				if (!obj) {
					printf("Error: dpvm %s object '%s' not found.\n", items[i].name, name);
					fclose(f);
					return -3 - (i << 2);
				}
				if (items[i].nlink >= 0) {
					if (obj->nlinks <= items[i].nlink) {
						printf("Error: dpvm %s object '%s' has no link %d.\n",
								items[i].name, name, items[i].nlink);
						fclose(f);
						return -4 - (i << 2);
					}
					obj = obj->links[items[i].nlink];
				}
				*items[i].ptr = obj;
				mask |= 1 << i;
				break;
			}
		}
	}

	fclose(f);

	for (i = 0; i < sizeof(items) / sizeof(struct configItem); i++) {
		if (!(mask & (1 << i))) {
			printf("Error: dpvm %s object not provided via set in '%s'.\n", items[i].name, PROFILE);
			return -5 - (i << 2);
		}
	}

	return 0;
}

struct dpvm_object *dpvm_config_update(struct dpvm_object *thread, struct dpvm_hash *hash, const char *itemName) {
	struct dpvm_object *obj = dpvm_hash2object(thread->dpvm, thread, hash);
	int i;

	if (!obj)
		return 0;

	for (i = 0; i < sizeof(items) / sizeof(struct configItem); i++) {
		if (!strcmp(items[i].name, itemName)) {
			if (items[i].nlink >= 0) {
				struct dpvm_object *old = obj;
				if (obj->nlinks <= items[i].nlink) {
					dpvm_unlink_object(thread, obj);
					return 0;
				}
				obj = obj->links[items[i].nlink];
				dpvm_link_object(obj);
				dpvm_unlink_object(thread, old);
			}
			if (dpvm_check_function(thread, obj, 0)) {
				dpvm_unlink_object(thread, obj);
				return 0;
			}
			return obj;
		}
	}

	return 0;
}
