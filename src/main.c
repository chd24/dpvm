/* dpvm: main; T15.395-T19.050; $DVS:time$ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include "history.h"
#include "init.h"
#include "io.h"
#include "launch.h"
#include "thread.h"

#define DPVM_PORT 15395

#ifdef __APPLE__
#define SIGPOLL SIGIO
#endif

static void ignore_signals(void) {
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGALRM, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGPOLL, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGVTALRM, SIG_IGN);
	signal(SIGPROF, SIG_IGN);
}

static void daemonize(void) {
	int i;
	if (getppid() == 1) exit(0); /* already a daemon */
	i = fork();
	if (i < 0) exit(1); /* fork error */
	if (i > 0) exit(0); /* parent exits */

	/* child (daemon) continues */
	setsid(); /* obtain a new process group */
	for (i = 2; i >= 0; --i) close(i); /* close all descriptors */
	i = open("/dev/null", O_RDWR);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result" 
	(void)dup(i); (void)dup(i); /* handle standard I/O */
#pragma GCC diagnostic pop

	/* first instance continues */
}

int main(int argc, char **argv) {
	struct dpvm dpvm;
	char *cmd = "";
	size_t size = 0;
        int64_t res = 0;
        int nactive;

	ignore_signals();
	
	printf("%s\r\n", DPVM_VERSION);
	
	if (argc > 1) {
		int i;
		if (argv[1][0] == '-' || !strcmp(argv[1], "help")) {
			printf("Usage: %s [expression...]\r\n", argv[0]);
			printf("With given expression(s) the program executes it in daemon mode.\r\n");
			printf("Without given expression(s) the program works in interactive mode.\r\n");
			printf("In any case, interactive terminal can be opened by telnet on port %d.\r\n", DPVM_PORT);
			printf("Type 'help' command to show list of interactive commands.\r\n");
			return 1;
		}

		for (i = 1; i < argc; ++i)
			size += strlen(argv[i]) + 1;

		cmd = malloc(size);
		if (!cmd) {
			printf("Error: memory allocation error.\n");
			return -1;
		}

		*cmd = 0;
		for (i = 1; i < argc; ++i) {
			if (i > 1) strcat(cmd, ";");
			strcat(cmd, argv[i]);
		}
		
		daemonize();
	}

	res = dpvm_init(&dpvm, 0);
	if (res) {
                printf("Error: init error 0x%lx.\n", res);
		return res;
	}

	res = dpvm_launch(&dpvm, cmd, DPVM_PORT);
	if (res) {
                printf("Error: launch error 0x%lx.\n", res);
		return res;
	}

	do {
		nactive = dpvm_thread_stats(&dpvm, DPVM_SYS_PARAM_NACTIVE);
		if (nactive) sleep(1);
	} while (nactive);

	dpvm_finish(&dpvm);
	
	return 0;
}
