/* dpvm: io; T15.530-T20.029; $DVS:time$ */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "bytecode.h"
#include "cache.h"
#include "init.h"
#include "input.h"
#include "io.h"
#include "mailbox.h"
#include "object.h"
#include "task.h"
#include "thread.h"

#define MAX_READ_SIZE		0x100000
#define INPUT_BUF_SIZE		0x1000
#define OUTPUT_BUF_SIZE		0x1000
#define PROMPT_MAX		0x200

struct dpvm_io {
	char input_buf[INPUT_BUF_SIZE];
	char output_buf[OUTPUT_BUF_SIZE];
	char prompt[PROMPT_MAX];
        pthread_mutex_t stdin_mutex;
        pthread_mutex_t stdout_mutex;
	int input_buf_size;
	int output_buf_size;
};

int dpvm_io_init(struct dpvm *dpvm) {
	int err;

	dpvm->io = calloc(sizeof(struct dpvm_io), 1);
	if (!dpvm->io)
		return -1;

	err = dpvm_mailbox_init(dpvm);
	if (err)
		return err * 10 - 2;

        pthread_mutex_init(&dpvm->io->stdin_mutex, NULL);
        pthread_mutex_init(&dpvm->io->stdout_mutex, NULL);

        dpvm_input_init();

	return 0;
}

int64_t dpvm_get_time() {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void dpvm_sleep_until(int64_t t) {
	struct timespec ts;
	ts.tv_sec  = t / 1000000000;
	ts.tv_nsec = t % 1000000000;
	while (t > dpvm_get_time()) {
#ifdef __APPLE__
		int64_t trem = t - dpvm_get_time();
		if (trem >= 0) {
			ts.tv_sec  = trem / 1000000000;
			ts.tv_nsec = trem % 1000000000;
			nanosleep(&ts, 0);
		}
#else
		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, 0);
#endif
	}
}

void dpvm_io_set_input_buf(struct dpvm *dpvm, const char *buf) {
	struct dpvm_io *io = dpvm->io;
	size_t size = strlen(buf);
	if (!size)
		return;
	if (size > INPUT_BUF_SIZE)
		size = INPUT_BUF_SIZE;

	pthread_mutex_lock(&io->stdin_mutex);
	memcpy(io->input_buf, buf, size);
	io->input_buf_size = size;
	pthread_mutex_unlock(&io->stdin_mutex);
}

static struct dpvm_object *input_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *data = 0, *func = thread->links[DPVM_THREAD_LINK_FUNC];
	int64_t err = -1, size = thread->ints[DPVM_THREAD_INT_SIZE], timeout = thread->ints[DPVM_THREAD_INT_EXT];

	if (size < 0) size = 0;
	else if (size > MAX_READ_SIZE) size = MAX_READ_SIZE;

	tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 2, 1, 0, 0);
	if (!tmp) goto end;

	data = dpvm_create_object(thread, tmp->links[1]->type, 0, 0, 0, size);
	if (!data) goto end;

	if (size) {
		if (size && thread->ints[DPVM_THREAD_INT_INPUT] == fileno(stdin)) {
                        struct dpvm_io *io = thread->dpvm->io;
                        char prompt[PROMPT_MAX];
			int promptSize;
			int editSize = 0;

                        pthread_mutex_lock(&io->stdout_mutex);
                        strcpy(prompt, io->prompt);
                        io->prompt[0] = 0;
                        pthread_mutex_unlock(&io->stdout_mutex);

			promptSize = strlen(prompt);
			while (editSize < promptSize && prompt[promptSize - editSize - 1] == '\b')
				editSize++;
			if (editSize) {
				promptSize -= editSize;
				if (editSize > promptSize)
					editSize = promptSize;
				promptSize -= editSize;
				if (editSize > size - 1)
					editSize = size - 1;
				prompt[promptSize + editSize] = 0;
			}
			if (editSize) {
				strcpy((char *)data->codes, prompt + promptSize);
				prompt[promptSize] = 0;
			} else
				data->codes[0] = 0;

			if (thread->dpvm->flags & DPVM_INIT_FLAG_IO_BUF) {
				int done = 0;
				while (!done) {
					pthread_mutex_lock(&io->stdin_mutex);
					if (io->input_buf_size) {
						size_t todo = io->input_buf_size;
						if (todo > size - 1)
							todo = size - 1;
						if (todo)
							memcpy((char *)data->codes, io->input_buf, todo);
						data->codes[todo] = 0;
						if (todo < (size_t)io->input_buf_size) {
							memmove(io->input_buf, io->input_buf + todo, io->input_buf_size - todo);
						}
						io->input_buf_size -= todo;
						done = 1;
					}
					pthread_mutex_unlock(&io->stdin_mutex);

					if (!done)
						usleep(100000);
					if (thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH)
						goto end;
				}
			} else {
				pthread_mutex_lock(&io->stdin_mutex);
				dpvm_input_read(&thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS],
					prompt, (char *)data->codes, size);
				pthread_mutex_unlock(&io->stdin_mutex);
			}

                        size = strlen((char *)data->codes);
                } else {
			struct pollfd pfd;
			int64_t delay, last = timeout + dpvm_get_time();
			pfd.fd = thread->ints[DPVM_THREAD_INT_INPUT];
			pfd.events = POLLIN;

			do {
				if (timeout < 0) delay = 1000000000;
				else delay = last - dpvm_get_time();
				if (delay < 0) delay = 0;
				else if (delay > 1000000000) delay = 1000000000;
				poll(&pfd, 1, delay / 1000000);
				if (thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS]
						& DPVM_TASK_FLAG_FINISH) goto end;
			} while (!pfd.revents && delay);

			if (pfd.revents & POLLIN)
				size = read(pfd.fd, data->codes, size);
			else
				size = -1ll;
		}
	}
	if (size < 0)
		data->ncodes = 0;
	else
		data->ncodes = size;

	if (dpvm_set_link(thread, tmp, 1, data)) goto end;
	tmp->ints[0] = size;
	err = 0;
end:
	if (data) dpvm_unlink_object(thread, data);
	if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
	return tmp;
}

size_t dpvm_io_get_output_buf(struct dpvm *dpvm, char *buf, size_t buf_size) {
	struct dpvm_io *io = dpvm->io;
	size_t todo = 0;

	if (dpvm->flags & DPVM_INIT_FLAG_IO_BUF) {
		todo = buf_size;
		pthread_mutex_lock(&io->stdout_mutex);
		if (todo > io->output_buf_size)
			todo = io->output_buf_size;
		if (todo) {
			memcpy(buf, io->output_buf, todo);
			if (todo < io->output_buf_size)
				memmove(io->output_buf, io->output_buf + todo, io->output_buf_size - todo);
			io->output_buf_size -= todo;
		}
		pthread_mutex_unlock(&io->stdout_mutex);
	}

	return todo;
}

static struct dpvm_object *output_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *data = thread->links[DPVM_THREAD_LINK_DATA],
			*func = thread->links[DPVM_THREAD_LINK_FUNC];
	int64_t done = 0, res;
	int out = thread->ints[DPVM_THREAD_INT_OUTPUT];

        if (out == fileno(stdout) && data->ncodes) {
                struct dpvm_io *io = thread->dpvm->io;
                ssize_t i, len, slen;

                for (i = data->ncodes - 1; i >= 0 && data->codes[i] != '\n'; i--);
                i++;
                len = data->ncodes - i;

                pthread_mutex_lock(&io->stdout_mutex);
                if (i)
                        io->prompt[0] = 0;
                slen = strlen(io->prompt);
                if (slen + len >= PROMPT_MAX)
                        len = PROMPT_MAX - 1 - slen;
                if (len)
                        memcpy(io->prompt + slen, data->codes + i, len),
                        io->prompt[slen + len] = 0;

		if (thread->dpvm->flags & DPVM_INIT_FLAG_IO_BUF) {
			if (io->output_buf_size < OUTPUT_BUF_SIZE) {
				size_t todo = OUTPUT_BUF_SIZE - io->output_buf_size;
				if (todo > data->ncodes)
					todo = data->ncodes;
				memcpy(io->output_buf + io->output_buf_size, data->codes, todo);
				io->output_buf_size += todo;
			}
			done = data->ncodes;
		}

                pthread_mutex_unlock(&io->stdout_mutex);
        }

	while (done < data->ncodes) {
		struct pollfd pfd;
		pfd.fd = out;
		pfd.events = POLLOUT;

		do {
			poll(&pfd, 1, 1000);
			if (thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH)
				goto end;
		} while (!pfd.revents);

		if (pfd.revents & POLLOUT) {
			res = write(out, data->codes + done, data->ncodes - done);
			if (res <= 0) break;
			done += res;
		} else
			break;
	}

	if (!data->ncodes && out > fileno(stderr) && !shutdown(out, SHUT_WR)) {
		char buf[4096];
		do {
			struct pollfd pfd;
			pfd.fd = out;
			pfd.events = POLLIN;

			do {
				poll(&pfd, 1, 1000);
				if (thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS]
						& DPVM_TASK_FLAG_FINISH) goto end;
			} while (!pfd.revents);

			if (pfd.revents & POLLIN)
				res = read(out, buf, 4096);
			else
				res = -1;
		} while (res > 0);
	}

	if (func->ncodes)
		tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 1, 1, 0, 0);
	if (tmp) tmp->ints[0] = done;
end:
	return tmp;
}

static struct dpvm_object *read_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *data = 0, *name = thread->links[DPVM_THREAD_LINK_NAME],
			*func = thread->links[DPVM_THREAD_LINK_FUNC];
	int64_t err = -1, pos = thread->ints[DPVM_THREAD_INT_POS], size = thread->ints[DPVM_THREAD_INT_SIZE], n;
	struct stat st;
	char buf[PATH_MAX];

	tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 2, 1, 0, 0);
	if (!tmp) goto end;

	if (size > 0 && pos >= 0 && name->ncodes > 0 && name->ncodes < PATH_MAX - 1) {
		memcpy(buf, name->codes, name->ncodes);
		buf[name->ncodes] = 0;
		if (stat(buf, &st) || pos >= st.st_size) size = 0;
		else if (pos + size > st.st_size) size = st.st_size - pos;
		if (size > MAX_READ_SIZE) size = MAX_READ_SIZE;
	} else size = 0;

	data = dpvm_create_object(thread, tmp->links[1]->type, 0, 0, 0, size);
	if (!data) goto end;

	if (size) {
		int fd = open(buf, O_RDONLY);
		if (fd >= 0) {
			size = pread(fd, data->codes, size, pos);
			if (size < 0) size = 0;
			close(fd);
		} else size = 0;
	}
	data->ncodes = size;

	if (dpvm_set_link(thread, tmp, 1, data)) goto end;
	tmp->ints[0] = size;
	err = 0;
end:
	if (data) dpvm_unlink_object(thread, data);
	if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
	return tmp;
}

static struct dpvm_object *write_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *name = thread->links[DPVM_THREAD_LINK_NAME],
			*data = thread->links[DPVM_THREAD_LINK_DATA], *func = thread->links[DPVM_THREAD_LINK_FUNC];
	int64_t pos = thread->ints[DPVM_THREAD_INT_POS], size = data->ncodes;

	if (name->ncodes > 0 && name->ncodes < PATH_MAX) {
		char buf[PATH_MAX];
		memcpy(buf, name->codes, name->ncodes);
		buf[name->ncodes] = 0;

		if (pos > 0 || size) {
			char *p;
			for (p = buf + 1; *p; ++p) {
				if (*p == '/') {
					*p = 0;
					mkdir(buf, 0755);
					*p = '/';
				}
			}
		}

		if (size) {
			int fd = open(buf, O_WRONLY | O_CREAT | (pos < 0 ? O_APPEND : 0), 0644);
			if (fd >= 0) {
				if (pos < 0)
					size = write(fd, data->codes, size);
				else
					size = pwrite(fd, data->codes, size, pos);
				close(fd);
			} else size = 0;
		} else if (pos > 0)
			truncate(buf, pos);
		else if (!pos) {
			char *p;
			remove(buf);
			for (p = buf + strlen(buf) - 1; p > buf; --p) {
				if (*p == '/') {
					*p = 0;
					rmdir(buf);
					*p = '/';
				}
			}
		}
	} else size = 0;

	if (func->ncodes)
		tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 1, 1, 0, 0);
	if (tmp) tmp->ints[0] = size;
	return tmp;
}

static struct dpvm_object *bind_run(struct dpvm_object *thread) {
	struct dpvm_object *t, *tmp = 0, *func = thread->links[DPVM_THREAD_LINK_FUNC];
	struct sockaddr_in addr;
	struct linger linger_opt = {1, 0}; /* Linger active, timeout 0 */
	socklen_t addr_len;
	int s0, s1, port, res;

	/* check parameters */
	if (thread->ints[DPVM_THREAD_INT_PORT] >> 16 != 0x406 /* ipv4, tcp */
			|| thread->ints[DPVM_THREAD_INT_IPADDR0] || thread->ints[DPVM_THREAD_INT_IPADDR1])
		goto end;

	/* Create a socket */
	s0 = socket(AF_INET, SOCK_STREAM, 0);
	if (s0 < 0) goto end;

	/* Detach from parent thread; set socket */
	dpvm_thread_unlink(thread->links[DPVM_THREAD_LINK_PARENT]);
	thread->links[DPVM_THREAD_LINK_PARENT] = thread;
	thread->ints[DPVM_THREAD_INT_INPUT] = s0;
	thread->ints[DPVM_THREAD_INT_OUTPUT] = s0;	

	/* Fill in the address structure containing self address */
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	port = thread->ints[DPVM_THREAD_INT_PORT] & 0xffff;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* Bind a socket to the address */
	res = bind(s0, (struct sockaddr *)&addr, sizeof(addr));
	if (res < 0) goto end;

	/* Set the "LINGER" timeout to zero, to close the listen socket
	   immediately at program termination. */
	setsockopt(s0, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt));

	/* Now, listen for a connection */
	res = listen(s0, 4096); /* 4096 is the maximal length of the queue */
	if (res < 0) goto end;

	dpvm_thread_change_nactive(thread->dpvm, -1);
	for(;;) {
		struct pollfd pfd;
		pfd.fd = s0;
		pfd.events = POLLIN;

		do {
			poll(&pfd, 1, 1000);			
			if (thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH)
				goto fin;
		} while (!pfd.revents);

		addr_len = sizeof(addr);
		s1 = accept(s0, (struct sockaddr *)&addr, &addr_len);
		if (s1 < 0) break;
		
		tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 1, 3, 0, 0);
		if (!tmp) break;
		if (dpvm_set_link(thread, tmp, 0, thread->links[DPVM_THREAD_LINK_ARG])) break;
		tmp->ints[0] = 4 << 24 | 6 << 16 | ntohs(addr.sin_port);
		tmp->ints[1] = ntohl(addr.sin_addr.s_addr);
		tmp->ints[2] = 0;
		t = dpvm_thread_create(func, tmp, 0, 0, thread,
			thread->ints[DPVM_THREAD_INT_FLAGS] | DPVM_THREAD_FLAG_NO_PARENT, s1, s1, 0, 0, 0);
		dpvm_unlink_object(thread, tmp); tmp = 0;
		if (!t) { close(s1); continue; }
		if (dpvm_thread_run(t, NULL)) dpvm_thread_unlink(t);
	}
fin:
	dpvm_thread_change_nactive(thread->dpvm, 1);

end:
	if (tmp) dpvm_unlink_object(thread, tmp);
	return 0;
}

static struct dpvm_object *connect_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *name = thread->links[DPVM_THREAD_LINK_NAME],
			*func = thread->links[DPVM_THREAD_LINK_FUNC];
	struct sockaddr_in addr;
	struct linger linger_opt = {1, 0}; /* Linger active, timeout 0 */
	struct hostent *host;
	socklen_t addr_len;
	char hostname[257];
	int64_t err = -1, ip0 = 0, ip1 = 0, port;
	int s0 = -1, res;

	/* check parameters */
	if (!name->ncodes || name->ncodes > 256) { err = ~0x100ll; goto reply; }
	memcpy(hostname, name->codes, name->ncodes);
	hostname[name->ncodes] = 0;
	host = gethostbyname(hostname);
	if (!host) { err = ~(0x200ll | (h_errno & 0xff)); goto reply; }
	if (!host->h_addr_list[0]) { err = ~(0x300ll | (h_errno & 0xff)); goto reply; }

	/* Fill in the address structure containing peer address */
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	memmove(&(addr.sin_addr.s_addr), host->h_addr_list[0], 4);
	ip0 = ntohl(addr.sin_addr.s_addr);

	if (thread->ints[DPVM_THREAD_INT_PORT] >> 16 != 0x406 /* ipv4, tcp */) 
		{ err = ~0x400ll; goto reply; }
	port = thread->ints[DPVM_THREAD_INT_PORT] & 0xffff;
	addr.sin_port = htons(port);

	/* Create a socket */
	s0 = socket(AF_INET, SOCK_STREAM, 0);
	if (s0 < 0) { err = ~(0x500ll | (errno & 0xff)); goto reply; }

	/* Set the "LINGER" timeout to zero, to close the listen socket
	   immediately at program termination. */
	setsockopt(s0, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt));

	/* Now, connect to remote server */
	res = connect(s0, (struct sockaddr*) &addr, sizeof(addr));
	if (res < 0) {
		close(s0); s0 = -1; 
		err = ~(0x600ll | (errno & 0xff));
	}

reply:
	tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 1, 3, 0, 0);
	if (!tmp) goto end;
	if (dpvm_set_link(thread, tmp, 0, thread->links[DPVM_THREAD_LINK_ARG])) goto end;
	if (s0 >= 0) {
		/* Detach from parent thread; set socket */
		dpvm_thread_unlink(thread->links[DPVM_THREAD_LINK_PARENT]);
		thread->links[DPVM_THREAD_LINK_PARENT] = thread;
		thread->ints[DPVM_THREAD_INT_INPUT] = s0;
		thread->ints[DPVM_THREAD_INT_OUTPUT] = s0;	
		tmp->ints[0] = 4 << 24 | 6 << 16 | port;
		tmp->ints[1] = ip0;
		tmp->ints[2] = ip1;
	} else {
		tmp->ints[0] = err;
	}
	err = 0;

end:
	if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
	return tmp;
}

static struct dpvm_object *getsys_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *data = thread->links[DPVM_THREAD_LINK_DATA], *result = 0,
			*func = thread->links[DPVM_THREAD_LINK_FUNC];
	uint64_t err = thread->ints[DPVM_THREAD_INT_EXT];
	int64_t i, n;
	int res = -1;

	tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 2, 0, 0, 0);
	if (!tmp) goto end;
	result = dpvm_create_object(thread, data->type, 0, data->nints, 0, 0);
	if (!result) goto end;
	if (dpvm_set_link(thread, tmp, 1, result)) goto end;

	for (i = 0; i < data->nints; ++i) {
		n = data->ints[i];
		if (n >= DPVM_SYS_PARAM_ERROR_END)
			n = dpvm_task_getsys(thread, n);
		else switch (n) {
			case DPVM_SYS_PARAM_TIME:
				n = dpvm_get_time(); 
				break;
			case DPVM_SYS_PARAM_MEMORY:
			case DPVM_SYS_PARAM_ALLOCED:
			case DPVM_SYS_PARAM_MAPPED:
			case DPVM_SYS_PARAM_NOBJECTS:
			case DPVM_SYS_PARAM_NTHREADS:
			case DPVM_SYS_PARAM_NACTIVE:
			case DPVM_SYS_PARAM_NPTHREADS:
			case DPVM_SYS_PARAM_QSIZE:
			case DPVM_SYS_PARAM_QTIME:
			case DPVM_SYS_PARAM_NTRANSACTIONS:
			case DPVM_SYS_PARAM_NRESULTS:
                                n = dpvm_thread_stats(thread->dpvm, n);
				break;
			case DPVM_SYS_PARAM_NADDRS:
			case DPVM_SYS_PARAM_NMESS:
				n = dpvm_mailbox_stats(thread->dpvm, n); 
				break;
			case DPVM_SYS_PARAM_ERROR:
				n = err & 0x1f;
				break;
			case DPVM_SYS_PARAM_CODE:
				n = err >> 5 & 0xfff;
				if (n >= 0xf00) n &= 0xff;
				else {
					struct dpvm_object *obj = dpvm_short_hash2object(thread->dpvm, thread, err >> 17);
					if (obj) {
						if (n < obj->ncodes) n = obj->codes[n];
						else n = -1;
						dpvm_unlink_object(thread, obj);
					} else n = -1;
				}
				break;
			case DPVM_SYS_PARAM_POS:
				n = err >> 5 & 0xfff;
				if (n >= 0xf00) n = -1;
				break;
			case DPVM_SYS_PARAM_FUNC_SHORT:
				n = err >> 17;
				break;
			default: 
				n = -1; 
				break;
		}
		result->ints[i] = n;
	}

	err = 0;
end:
	if (result) dpvm_unlink_object(thread, result);
	if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
	return tmp;
}

static struct dpvm_object *setsys_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *vars = thread->links[DPVM_THREAD_LINK_NAME],
		*values = thread->links[DPVM_THREAD_LINK_DATA], *result = 0, *func = thread->links[DPVM_THREAD_LINK_FUNC];
	int64_t i, var, value, err = -1;

	if (vars->nints != values->nints) goto end;
	result = dpvm_create_object(thread, values->type, 0, values->nints, 0, 0);
	if (!result) goto end;

	for (i = 0; i < vars->nints; ++i) {
		var = vars->ints[i]; value = values->ints[i];
		if (var >= DPVM_SYS_PARAM_ERROR_END)
			value = dpvm_task_setsys(thread, var, value);
		else switch (var) {
			case DPVM_SYS_PARAM_TIME:
				for(;;) {
					int64_t t = dpvm_get_time();
					if (t >= value) break;
					t += 1000000000;
					if (t >= value) t = value;
					dpvm_sleep_until(t);
					if (thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS]
							& DPVM_TASK_FLAG_FINISH) goto end;
				}
				value = dpvm_get_time(); break;
			default: 
				value = -1; break;
		}
		result->ints[i] = value;
	}

	if (func->ncodes)
		tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 2, 0, 0, 0);
	if (!tmp) goto end;
	if (dpvm_set_link(thread, tmp, 1, result)) goto end;

	err = 0;
end:
	if (result) dpvm_unlink_object(thread, result);
	if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
	return tmp;
}

#ifdef __APPLE__
#define st_mtim st_mtimespec
#define st_atim st_atimespec
#define st_ctim st_ctimespec
#endif

static int file2stat(struct dpvm_object *thread, struct dpvm_object *result, char *path, char *name) {
	struct stat st;
	struct dpvm_object *s;
	int res;
	s = dpvm_create_object(thread, dpvm_type_of_link(result->type, result->nlinks), 0, 8, 0, strlen(name));
	if (!s) return 0;
	res = dpvm_push_link(thread, result, s);
	dpvm_unlink_object(thread, s);
	if (res) return 0;
	if (s->ncodes) memcpy(s->codes, name, s->ncodes);
	if (path && !stat(path, &st)) {
		s->ints[0] = st.st_size;
		s->ints[1] = st.st_mtime * 1000000000l + st.st_mtim.tv_nsec;
		s->ints[2] = st.st_atime * 1000000000l + st.st_atim.tv_nsec;
		s->ints[3] = st.st_ctime * 1000000000l + st.st_ctim.tv_nsec;
		s->ints[4] = st.st_mode;
		s->ints[5] = st.st_ino;
		s->ints[6] = st.st_uid;
		s->ints[7] = st.st_gid;
	}
	return 1;
}

static struct dpvm_object *mload_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *data = thread->links[DPVM_THREAD_LINK_DATA], *result = 0,
			*func = thread->links[DPVM_THREAD_LINK_FUNC], *obj;
	int64_t i, n, err = -1;

	if (data->nints & 3) goto end;
	n = data->nints >> 2;
	tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 2, 0, 0, 0);
	if (!tmp) goto end;
	result = dpvm_create_object(thread, dpvm_type_of_link(tmp->type, 1), n, 0, 0, 0);
	if (!result) goto end;
	if (dpvm_set_link(thread, tmp, 1, result)) goto end;

	for (i = 0; i < n; i++) {
		int64_t *h = &data->ints[i << 2];
		if (h[0] >> 47 || h[1] || h[2] || h[3])
			obj = dpvm_hash2object(thread->dpvm, thread, (struct dpvm_hash *)h);
		else
			obj = dpvm_short_hash2object(thread->dpvm, thread, h[0]);
		if (obj) {
			if (dpvm_set_link(thread, result, i, obj)) goto end;
			dpvm_unlink_object(thread, obj);
		} else
			if (dpvm_set_link(thread, result, i, thread->dpvm->any)) goto end;
	}

	err = 0;
end:
	if (result) dpvm_unlink_object(thread, result);
	if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
	return tmp;
}

static struct dpvm_object *msave_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *data = thread->links[DPVM_THREAD_LINK_DATA], *result = 0,
			*func = thread->links[DPVM_THREAD_LINK_FUNC], *obj;
	int64_t i, n, err = -1;

	n = data->nlinks;
	tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 2, 0, 0, 0);
	if (!tmp) goto end;
	result = dpvm_create_object(thread, dpvm_type_of_link(tmp->type, 1), n, 0, 0, 0);
	if (!result) goto end;
	if (dpvm_set_link(thread, tmp, 1, result)) goto end;

	for (i = 0; i < n; i++) {
		if (dpvm_set_link(thread, result, i, data->links[i])) goto end;
		if (dpvm_cache_add(thread->dpvm, thread, &result->links[i])
				&& dpvm_set_link(thread, result, i, thread->dpvm->any)) goto end;
	}

	err = 0;
end:
	if (result) dpvm_unlink_object(thread, result);
	if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
	return tmp;
}

static struct dpvm_object *mstat_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *name = thread->links[DPVM_THREAD_LINK_NAME], *result = 0,
			*func = thread->links[DPVM_THREAD_LINK_FUNC];
	int64_t err = -1, pos = thread->ints[DPVM_THREAD_INT_POS], size = thread->ints[DPVM_THREAD_INT_SIZE];
	
	tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 2, 0, 0, 0);
	if (!tmp) goto end;
	result = dpvm_create_object(thread, dpvm_type_of_link(tmp->type, 1), 0, 0, 0, 0);
	if (!result) goto end;
	if (dpvm_set_link(thread, tmp, 1, result)) goto end;

	if (size > 0 && pos >= 0 && name->ncodes > 0 && name->ncodes < PATH_MAX - 1) {
		char buf[PATH_MAX], *ptr, *path;
		memcpy(buf, name->codes, name->ncodes);
		buf[name->ncodes] = 0;
		if ((ptr = strchr(buf, '*'))) {
			if (!ptr[1] && ptr > buf && ptr[-1] == '/') {
				DIR *d;
				*ptr = 0;
				if ((d = opendir(buf))) {
					struct dirent *de;
					if (size > MAX_READ_SIZE) size = MAX_READ_SIZE;
					while (size && (de = readdir(d))) {
						if (pos) { pos--; continue; }
						if (ptr - buf + strlen(de->d_name) < PATH_MAX) {
							strcpy(ptr, de->d_name);
							path = buf;
						} else path = 0;
						size -= file2stat(thread, result, path, de->d_name);
					}
					closedir(d);
				}
			}
		} else if (!pos) file2stat(thread, result, buf, "");
	}

	err = 0;
end:
	if (result) dpvm_unlink_object(thread, result);
	if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
	return tmp;
}

static struct dpvm_object *mrecv_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *address = thread->links[DPVM_THREAD_LINK_NAME], *result = 0,
			*func = thread->links[DPVM_THREAD_LINK_FUNC];
	int64_t err = -1, res, timeout = thread->ints[DPVM_THREAD_INT_EXT];

	result = dpvm_mailbox_receive(thread, address, timeout, &res);
	if (func->ncodes)
		tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 2, 1, 0, 0);
	if (!tmp) goto end;
	tmp->ints[0] = res;
	if (dpvm_set_link(thread, tmp, 1, result)) goto end;

	err = 0;
end:
	if (result) dpvm_unlink_object(thread, result);
	if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
	return tmp;
}

static struct dpvm_object *msend_run(struct dpvm_object *thread) {
	struct dpvm_object *tmp = 0, *address = thread->links[DPVM_THREAD_LINK_NAME],
			*message = thread->links[DPVM_THREAD_LINK_DATA], *func = thread->links[DPVM_THREAD_LINK_FUNC];
	int64_t err = -1;
	int status = dpvm_mailbox_send(thread, address, message);

	if (func->ncodes)
		tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 1, 1, 0, 0);
	if (!tmp) goto end;
	tmp->ints[0] = status;

	err = 0;
end:
	if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
	return tmp;
}

#define int enum {
#define _Z 0};
#include "../dpvm/common/mpopenFlags.dpvmh"
#undef _Z
#undef int

static struct dpvm_object *mpopen_run(struct dpvm_object *thread) {
    struct dpvm_object *threadW = 0, *threadI = 0, *tmp = 0, *tmpW = 0, *tmpI = 0,
		*name = thread->links[DPVM_THREAD_LINK_NAME], *data = thread->links[DPVM_THREAD_LINK_DATA],
		*arg  = thread->links[DPVM_THREAD_LINK_ARG ], *func = thread->links[DPVM_THREAD_LINK_FUNC];
    int64_t err = -1l, status = DPVM_MPOPEN_READ, flags = thread->ints[DPVM_THREAD_INT_EXT],
		mask = flags & ~(int64_t)DPVM_MPOPEN_INTERNAL;
    int fd[4] = { -1, -1, -1, -1 }, i, in = thread->ints[DPVM_THREAD_INT_INPUT],
		out = thread->ints[DPVM_THREAD_INT_OUTPUT];

    tmp = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 1, 1, 0, 0);
    if (!tmp) goto end;

    if (mask != DPVM_MPOPEN_READ && mask != DPVM_MPOPEN_RW && mask != DPVM_MPOPEN_RW_MERGED) { status = -1l; goto report; }
    if (pipe(fd)) { status = -2l; goto report; }
    if (mask == DPVM_MPOPEN_RW || mask == DPVM_MPOPEN_RW_MERGED) {
        if (pipe(fd + 2)) { status = -3l; goto report; }
        in = fd[2];
    }

    if (flags & DPVM_MPOPEN_INTERNAL) { /* internal task */
        tmpI = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 1, 1, 0, 0);
        if (!tmpI) { status = -8l; goto report; }
        if (dpvm_set_link(thread, tmpI, 0, arg)) { status = -9l; goto report; }
        tmpI->ints[0] = DPVM_MPOPEN_INTERNAL;
        threadI = dpvm_thread_create(func, tmpI, 0, 0, thread, 0, 0, 0, 0, 0, 0);
        if (!threadI) { status = -10l; goto report; }
        if (fd[2] >= 0)
            threadI->ints[DPVM_THREAD_INT_INPUT] = fd[2]; fd[2] = -1;
        threadI->ints[DPVM_THREAD_INT_OUTPUT] = fd[1]; fd[1] = -1;
        if (dpvm_thread_run(threadI, NULL)) { status = -11l; goto report; }
        threadI = 0;

    } else { /* external system task */
        char buf[PATH_MAX];
        pid_t pid;
        struct stat st;
        int fdmax = sysconf(_SC_OPEN_MAX);

        if (!name->ncodes || name->ncodes >= PATH_MAX) { status = -12l; goto report; }
        memcpy(buf, name->codes, name->ncodes);
        buf[name->ncodes] = 0;
        if (stat(buf, &st)) { status = -13l; goto report; }
        if (S_ISDIR(st.st_mode)) { status = -14l; goto report; }
        if (!(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) { status = -15l; goto report; }

        if (!(pid = fork())) {
            char **argv, **envp, *str;
            int j = 0, k = 0, isenv = 1;
            argv = (char **)malloc((data->nlinks + 2) * sizeof(char *));
            envp = (char **)malloc((data->nlinks + 1) * sizeof(char *));
            if (!argv || !envp) exit(-1);
            argv[j++] = buf;
            for (i = 0; i < data->nlinks; ++i) {
                char *ptr;
                str = (char *)malloc(data->links[i]->ncodes + 1);
                if (!str) exit(-1);
                memcpy(str, data->links[i]->codes, data->links[i]->ncodes);
                str[data->links[i]->ncodes] = 0;
                if ((ptr = strchr(str, '='))) {
                    while (ptr-- > str) {
                        int c = (unsigned char)*ptr;
                        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) {
                            isenv = 0;
                            break;
                        }
                    }
                } else
                    isenv = 0;
                if (isenv)
                    envp[k++] = str;
                else
                    argv[j++] = str;
            }
            argv[j++] = 0; envp[k++] = 0;
            if (in > 2)
                close(0);
            close(1);
            if (mask == DPVM_MPOPEN_RW_MERGED && out > 2)
                close(2);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
            if (in > 2)
                (void)dup(in);
            (void)dup(fd[1]);
            if (mask == DPVM_MPOPEN_RW_MERGED && out > 2)
                (void)dup(out);
#pragma GCC diagnostic pop
            for (i = 3; i < fdmax; ++i) close(i); /* close all descriptors */
            if (execve(buf, argv, envp)) exit(-1);
        }
        if (pid < 0) { status = -16l; goto report; }
        thread->ints[DPVM_THREAD_INT_CHILD] = pid;
    }

    if (mask == DPVM_MPOPEN_RW) {
        tmpW = dpvm_create_object(thread, func->type->links[DPVM_TYPE_IN_ARGS], 1, 1, 0, 0);
        if (!tmpW) { status = -4l; goto report; }
        if (dpvm_set_link(thread, tmpW, 0, arg)) { status = -5l; goto report; }
        tmpW->ints[0] = DPVM_MPOPEN_WRITE;
        threadW = dpvm_thread_create(func, tmpW, 0, 0, thread, 0, 0, 0, 0, 0, 0);
        if (!threadW) { status = -6l; goto report; }
        threadW->ints[DPVM_THREAD_INT_OUTPUT] = fd[3]; fd[3] = -1;
        if (dpvm_thread_run(threadW, NULL)) { status = -7l; goto report; }
        threadW = 0;
    } else if (mask == DPVM_MPOPEN_RW_MERGED) {
        thread->ints[DPVM_THREAD_INT_OUTPUT] = fd[3]; fd[3] = -1;
    }
    thread->ints[DPVM_THREAD_INT_INPUT] = fd[0]; fd[0] = -1;

report:
	tmp->ints[0] = status;
	err = 0;

end:
	for (i = 0; i < 4; ++i) if (fd[i] >= 0) close(fd[i]);
    if (err && tmp) dpvm_unlink_object(thread, tmp), tmp = 0;
    if (tmpW) dpvm_unlink_object(thread, tmpW);
    if (threadW) dpvm_thread_unlink(threadW);
    if (tmpI) dpvm_unlink_object(thread, tmpI);
    if (threadI) dpvm_thread_unlink(threadI);
    return tmp;
}

const struct dpvm_io_run_func dpvm_io_run_funcs[] = {
	{ DPVM_CODE_INPUT,   &input_run   },
	{ DPVM_CODE_OUTPUT,  &output_run  },
	{ DPVM_CODE_READ,    &read_run    },
	{ DPVM_CODE_WRITE,   &write_run   },
	{ DPVM_CODE_BIND,    &bind_run    },
	{ DPVM_CODE_CONNECT, &connect_run },
	{ DPVM_CODE_GETSYS,  &getsys_run  },
	{ DPVM_CODE_SETSYS,  &setsys_run  },
	{ DPVM_CODE_MLOAD,   &mload_run   },
	{ DPVM_CODE_MSAVE,   &msave_run   },
	{ DPVM_CODE_MSTAT,   &mstat_run   },
	{ DPVM_CODE_MRECV,   &mrecv_run   },
	{ DPVM_CODE_MSEND,   &msend_run   },
	{ DPVM_CODE_MPOPEN,  &mpopen_run  },
	{ -1, 0 }
};
