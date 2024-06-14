/* dpvm: input; T15.422-T18.498; $DVS:time$ */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "../linenoise/linenoise.h"
#include "input.h"

#define int enum {
#define _Z 0};
#include "../dpvm/common/taskFlags.dpvmh"
#undef _Z
#undef int

#define COMMAND_HISTORY ".dpvm_history.txt"

static const char *commands[] = {
#define cmd_(c,a,d) #c,
#include "commands.h"
#undef cmd_
	0
};

int dpvm_input_read(volatile int64_t *taskFlags, const char *prompt, char *cmd, size_t maxlen) {
	char *line;
	int err = 0;
	int added = 0;

	if (*taskFlags & DPVM_TASK_FLAG_NO_INPUT_ECHO) {
		struct termios t[1];
		tcgetattr(0, t);
		t->c_lflag &= ~ECHO;
		tcsetattr(0, TCSANOW, t);
		line = fgets(cmd, maxlen, stdin);
		t->c_lflag |= ECHO;
		tcsetattr(0, TCSANOW, t);
		printf("\n");
	} else {
		if (*cmd) {
			linenoiseHistoryAdd(cmd);
			added = 1;
		}

		if (*prompt)
			write(fileno(stdin), "\r", 1);

		// TODO: inspect finish task flag and interrupt input
		// TODO: implement utf8 support
		line = linenoise(prompt);

		if (line) {
			if (strlen(line) >= maxlen) {
				printf("exceed max length\n");
				if (maxlen > 1)
					memcpy(cmd, line, maxlen - 1);
				cmd[maxlen - 1] = '\0';
				err = -2;
			} else {
				strcpy(cmd, line);
			}

			free(line);
		}
	}

	if (!line) {
		*cmd = 0;
		err = -1;
	}

	if (*taskFlags & DPVM_TASK_FLAG_NO_INPUT_HISTORY) {
		if (added)
			linenoiseHistoryLoad(COMMAND_HISTORY);
	} else if (*cmd) {
		linenoiseHistoryAdd(cmd);
		linenoiseHistorySave(COMMAND_HISTORY);
	}

	return err;
}

static void dpvm_com_completion(const char *buf, linenoiseCompletions *lc) {
	int index;
	for (index = 0; commands[index]; index++) {
		if (!strncmp(buf, commands[index], strlen(buf))) {
			linenoiseAddCompletion(lc, commands[index]);
		}
	}
}

void dpvm_input_init(void) {
	linenoiseSetCompletionCallback(dpvm_com_completion); //set completion
	linenoiseHistorySetMaxLen(0x10000); //set max line for history
	linenoiseHistoryLoad(COMMAND_HISTORY); //load history
}
