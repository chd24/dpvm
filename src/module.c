/* dpvm: entry point to dpvm module; T15.395-T18.511; $DVS:time$ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "history.h"
#include "init.h"
#include "io.h"
#include "launch.h"
#include "thread.h"

#define DPVM_PORT	15395
#define MESS_SIZE	0x200

const char *dpvmGetStr(const char *input) {
	static struct dpvm dpvm;
	static char mess[MESS_SIZE];
	static int state = 0;

	switch (state) {
	case 0:
		state = 1;
		sprintf(mess, "%s\n", DPVM_VERSION);
		return mess;
	case 1:
		{
			unsigned long long res = dpvm_init(&dpvm, DPVM_INIT_FLAG_IO_BUF);
			if (res) {
				sprintf(mess, "Error: init error 0x%llx.\n", res);
				state = -1;
				return mess;
			}

			res = dpvm_launch(&dpvm, "", DPVM_PORT);
			if (res) {
				sprintf(mess, "Error: launch error 0x%llx.\n", res);
				state = -1;
				return mess;
			}

		}
		state = 2;
		return "DPVM launched.\n";
	case 2:
		if (*input) dpvm_io_set_input_buf(&dpvm, input);
		mess[dpvm_io_get_output_buf(&dpvm, mess, MESS_SIZE - 1)] = 0;
		return mess;
	default:
		break;
	}

	return "";
}
