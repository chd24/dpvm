/* dpvm: launch monitor task; T15.401-T19.630; $DVS:time$ */

#include <stdio.h>
#include <string.h>
#include "bytecode.h"
#include "error.h"
#include "launch.h"
#include "object.h"
#include "run.h"
#include "thread.h"

static int64_t run_program(struct dpvm_object *function, struct dpvm_object *arg, struct dpvm_object **res) {
	struct dpvm_object *obj;
        int64_t err = 0;

	obj = *res = dpvm_thread_create(function, arg, 0, 0, 0,
			DPVM_THREAD_FLAG_IO, fileno(stdin), fileno(stdout), 0, 0, 0);
	if (!obj) err = DPVM_ERROR_CREATE_OBJECT;
	if (!err) err = dpvm_thread_start(obj, NULL);
	if (!err) {
		do {
			err = dpvm_run_thread(obj, 0x1000);
			if ((!err) & dpvm_sigint) {
				err = DPVM_ERROR_TERMINATED;
				dpvm_sigint = 0;
			}
		} while (!err);
		if (err == DPVM_ERROR_FINISHED) err = 0;
	}
	if (!err) err = dpvm_thread_get_result(res);
	if (err) { dpvm_thread_unlink(obj); *res = 0; }
	return err;
}

int64_t dpvm_launch(struct dpvm *dpvm, char *cmd, int port) {
	struct dpvm_object *str = 0, *arg = 0, *res = 0;
        int64_t err;

	arg = dpvm_create_object(0, dpvm->monitor->type->links[DPVM_TYPE_IN_ARGS], 1, 1, 0, 0);
	if (!arg) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }

	str = dpvm_create_object(0, dpvm_type_of_link(dpvm->monitor->type->links[DPVM_TYPE_IN_ARGS], 0),
		0, 0, 0, strlen(cmd));
	if (!str) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }

	memcpy(str->codes, cmd, str->ncodes);
	err = dpvm_set_link(0, arg, 0, str);
	if (err) goto end;

	arg->ints[0] = port;

	err = run_program(dpvm->monitor, arg, &res);

end:
	if (res) dpvm_unlink_object(0, res);
	if (str) dpvm_unlink_object(0, str);
	if (arg) dpvm_unlink_object(0, arg);

	return err;
}
