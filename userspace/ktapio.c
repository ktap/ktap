/*
 * ktapio.c - relay transport in userspace
 *
 * Copyright 2013 The ktap Project Developers.
 * See the COPYRIGHT file at the top-level directory of this distribution.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <pthread.h>

#define MAX_BUFLEN  131072
#define PATH_MAX 128

#define handle_error(str) do { perror(str); exit(-1); } while(0)

extern int ktap_fd;
extern int use_ftrace_rb;
extern pid_t ktap_pid;

void sigfunc(int signo)
{
	/* should not not reach here */
}

static void block_sigint()
{
	sigset_t mask, omask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	pthread_sigmask(SIG_BLOCK, &mask, &omask);
}

static void *reader_thread(void *data)
{
	char buf[MAX_BUFLEN];
	struct pollfd pollfd[2];
	struct timespec tim = {.tv_sec=0, .tv_nsec=200000000};
	int timeout, fd, ret, len;
	int cpu = (int)(long)data;
	char filename[PATH_MAX];
	int exiting = 0;

	block_sigint();

	if (use_ftrace_rb)
		sprintf(filename, "/sys/kernel/debug/tracing/trace");
	else
		sprintf(filename, "/sys/kernel/debug/ktap/trace-%d-%d", ktap_pid, cpu);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open %s failed\n", filename);
		return NULL;
	}

	pollfd[0].fd = fd;
	pollfd[0].events = POLLIN;
	pollfd[1].fd = ktap_fd;
	pollfd[1].events = POLLIN;

	timeout = tim.tv_sec * 1000 + tim.tv_nsec / 1000000;

	do {
		ret = poll(&pollfd[0], 2, timeout);
		if (ret < 0)
			break;

		/* ktapvm is waiting for reader to read all remain content */
		if (pollfd[1].revents == POLLERR) {
			if (use_ftrace_rb)
				break;
			exiting = 1;
		}

		while ((len = read(fd, buf, sizeof(buf))) > 0)
			write(1, buf, len);

		if (exiting == 1)
			break;
	} while (1);

	close(fd);

	return NULL;
}


static pthread_t *reader;
static long ncpus = -1;

static void *manager_thread(void *data)
{
	void (*ktap_io_ready_cb)(void) = data;
	int i;

	block_sigint();

	if (use_ftrace_rb)
		goto out;

	ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpus < 0)
		handle_error("cannot get cpu number\n");

	reader = calloc(ncpus, sizeof(pthread_t));
	if (!reader)
		handle_error("cannot malloc\n");
		
	for (i = 0; i < ncpus; i++) {
		if (pthread_create(&reader[i], NULL, reader_thread, (void *)(long)i) < 0)
			handle_error("pthread_create reader_thread failed\n");

		if (use_ftrace_rb)
			break;
	}

	for (i = 0; i < ncpus; i++) {
		if (!reader[i])
			continue;
		pthread_join(reader[i], NULL);
	}

 out:
	(*ktap_io_ready_cb)();
}

int ktapio_create(void *cb)
{
	pthread_t manager;

	signal(SIGINT, sigfunc);

	if (pthread_create(&manager, NULL, manager_thread, cb) < 0)
		handle_error("pthread_create manager_thread failed\n");

	return 0;
}


