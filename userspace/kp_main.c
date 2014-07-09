/*
 * main.c - ktap compiler and loader entry
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
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
#include <sched.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <linux/errno.h>

#include "../include/ktap_types.h"
#include "kp_lex.h"
#include "kp_parse.h"
#include "kp_symbol.h"
#include "cparser.h"

static void usage(const char *msg_fmt, ...)
{
	va_list ap;

	va_start(ap, msg_fmt);
	vfprintf(stderr, msg_fmt, ap);
	va_end(ap);

	fprintf(stderr,
"Usage: ktap [options] file [script args] -- cmd [args]\n"
"   or: ktap [options] -e one-liner  -- cmd [args]\n"
"\n"
"Options and arguments:\n"
"  -o file        : send script output to file, instead of stderr\n"
"  -p pid         : specific tracing pid\n"
"  -C cpu         : cpu to monitor in system-wide\n"
"  -T             : show timestamp for event\n"
"  -V             : show version\n"
"  -v             : enable verbose mode\n"
"  -q             : suppress start tracing message\n"
"  -d             : dry run mode(register NULL callback to perf events)\n"
"  -s             : simple event tracing\n"
"  -b             : list byte codes\n"
"  -le [glob]     : list pre-defined events in system\n"
#ifndef NO_LIBELF
"  -lf DSO        : list available functions from DSO\n"
"  -lm DSO        : list available sdt notes from DSO\n"
#endif
"  file           : program read from script file\n"
"  -- cmd [args]  : workload to tracing\n");

	exit(EXIT_FAILURE);
}

#define handle_error(str) do { perror(str); exit(-1); } while(0)

ktap_option_t uparm;
static int ktap_trunk_mem_size = 1024;

static int kp_writer(const void* p, size_t sz, void* ud)
{
	if (uparm.trunk_len + sz > ktap_trunk_mem_size) {
		int new_size = (uparm.trunk_len + sz) * 2;
		uparm.trunk = realloc(uparm.trunk, new_size);
		ktap_trunk_mem_size = new_size;
	}

	memcpy(uparm.trunk + uparm.trunk_len, p, sz);
	uparm.trunk_len += sz;

	return 0;
}


static int forks;
static char **workload_argv;

static int fork_workload(int ktap_fd)
{
	int pid;

	pid = fork();
	if (pid < 0)
		handle_error("failed to fork");

	if (pid > 0)
		return pid;

	signal(SIGTERM, SIG_DFL);

	execvp("", workload_argv);

	/*
	 * waiting ktapvm prepare all tracing event
	 * make it more robust in future.
	 */
	pause();

	execvp(workload_argv[0], workload_argv);

	perror(workload_argv[0]);
	exit(-1);

	return -1;
}

#define KTAPVM_PATH "/sys/kernel/debug/ktap/ktapvm"

static char *output_filename;

static int run_ktapvm()
{
        int ktapvm_fd, ktap_fd;
	int ret;

	ktapvm_fd = open(KTAPVM_PATH, O_RDONLY);
	if (ktapvm_fd < 0)
		handle_error("open " KTAPVM_PATH " failed");

	ktap_fd = ioctl(ktapvm_fd, 0, NULL);
	if (ktap_fd < 0)
		handle_error("ioctl ktapvm failed");

	kp_create_reader(output_filename);

	if (forks) {
		uparm.trace_pid = fork_workload(ktap_fd);
		uparm.workload = 1;
	}

	ret = ioctl(ktap_fd, KTAP_CMD_IOC_RUN, &uparm);
	switch (ret) {
	case -EPERM:
	case -EACCES:
		fprintf(stderr, "You may not have permission to run ktap\n");
		break;
	}

	close(ktap_fd);
	close(ktapvm_fd);

	return ret;
}

int verbose;
static int quiet;
static int dry_run;
static int dump_bytecode;
static char oneline_src[1024];
static int trace_pid = -1;
static int trace_cpu = -1;
static int print_timestamp;

#define SIMPLE_ONE_LINER_FMT	\
	"trace %s { print(cpu(), tid(), execname(), argstr) }"

static const char *script_file;
static int script_args_start;
static int script_args_end;

#ifndef NO_LIBELF
struct binary_base
{
	int type;
	const char *binary;
};
static int print_symbol(const char *name, vaddr_t addr, void *arg)
{
	struct binary_base *base = (struct binary_base *)arg;
	const char *type = base->type == FIND_SYMBOL ?
		"probe" : "sdt";

	printf("%s:%s:%s\n", type, base->binary, name);
	return 0;
}
#endif

static void parse_option(int argc, char **argv)
{
	char pid[32] = {0};
	char cpu_str[32] = {0};
	char *next_arg;
	int i, j;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			script_file = argv[i];
			if (!script_file)
				usage("");

			script_args_start = i + 1;
			script_args_end = argc;

			for (j = i + 1; j < argc; j++) {
				if (argv[j][0] == '-' && argv[j][1] == '-')
					goto found_cmd;
			}

			return;
		}

		if (argv[i][0] == '-' && argv[i][1] == '-') {
			j = i;
			goto found_cmd;
		}

		next_arg = argv[i + 1];

		switch (argv[i][1]) {
		case 'o':
			output_filename = malloc(strlen(next_arg) + 1);
			if (!output_filename)
				return;

			strncpy(output_filename, next_arg, strlen(next_arg));
			i++;
			break;
		case 'e':
			strncpy(oneline_src, next_arg, strlen(next_arg));
			i++;
			break;
		case 'p':
			strncpy(pid, next_arg, strlen(next_arg));
			trace_pid = atoi(pid);
			i++;
			break;
		case 'C':
			strncpy(cpu_str, next_arg, strlen(next_arg));
			trace_cpu = atoi(cpu_str);
			i++;
			break;
		case 'T':
			print_timestamp = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'd':
			dry_run = 1;
			break;
		case 's':
			sprintf(oneline_src, SIMPLE_ONE_LINER_FMT, next_arg);
			i++;
			break;
		case 'b':
			dump_bytecode = 1;
			break;
		case 'l': /* list available events */
			switch (argv[i][2]) {
			case 'e': /* tracepoints */
				list_available_events(next_arg);
				exit(EXIT_SUCCESS);
#ifndef NO_LIBELF
			case 'f': /* functions in DSO */
			case 'm': /* static marks in DSO */ {
				const char *binary = next_arg;
				int type = argv[i][2] == 'f' ?
						FIND_SYMBOL : FIND_STAPSDT_NOTE;
				struct binary_base base = {
					.type = type,
					.binary = binary,
				};
				int ret;

				ret = parse_dso_symbols(binary, type,
							print_symbol,
							(void *)&base);
				if (ret <= 0) {
					fprintf(stderr,
					"error: no symbols in binary %s\n",
						binary);
					exit(EXIT_FAILURE);
				}
				exit(EXIT_SUCCESS);
			}
#endif
			default:
				exit(EXIT_FAILURE);
			}
			break;
		case 'V':
#ifdef CONFIG_KTAP_FFI
			usage("%s (with FFI)\n\n", KTAP_VERSION);
#else
			usage("%s\n\n", KTAP_VERSION);
#endif
			break;
		case '?':
		case 'h':
			usage("");
			break;
		default:
			usage("wrong argument\n");
			break;
		}
	}

	return;

 found_cmd:
	script_args_end = j;
	forks = 1;
	workload_argv = &argv[j + 1];
}

static ktap_proto_t *parse(const char *chunkname, const char *src)
{
	LexState ls;

	ls.chunkarg = chunkname ? chunkname : "?";
	kp_lex_init();
	kp_buf_init(&ls.sb);
	kp_lex_setup(&ls, src);
	return kp_parse(&ls);
}

static void compile(const char *input)
{
	ktap_proto_t *pt;
	char *buff;
	struct stat sb;
	int fdin;

	kp_str_resize();

	if (oneline_src[0] != '\0') {
		ffi_cparser_init();
		pt = parse(input, oneline_src);
		goto dump;
	}

	fdin = open(input, O_RDONLY);
	if (fdin < 0) {
		fprintf(stderr, "open file %s failed\n", input);
		exit(-1);
	}

	if (fstat(fdin, &sb) == -1)
		handle_error("fstat failed");

	buff = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fdin, 0);
	if (buff == MAP_FAILED)
		handle_error("mmap failed");

	ffi_cparser_init();
	pt = parse(input, buff);

	munmap(buff, sb.st_size);
	close(fdin);

 dump:
	if (dump_bytecode) {
#ifdef CONFIG_KTAP_FFI
		kp_dump_csymbols();
#endif
		kp_dump_proto(pt);
		exit(0);
	}

	/* bcwrite */
	uparm.trunk = malloc(ktap_trunk_mem_size);
	if (!uparm.trunk)
		handle_error("malloc failed");

	kp_bcwrite(pt, kp_writer, NULL, 0);
	ffi_cparser_free();
}

int main(int argc, char **argv)
{
	char **ktapvm_argv;
	int new_index, i;
	int ret;

	if (argc == 1)
		usage("");

	parse_option(argc, argv);

	if (oneline_src[0] != '\0')
		script_file = "(command line)";

	compile(script_file);

	ktapvm_argv = (char **)malloc(sizeof(char *)*(script_args_end -
					script_args_start + 1));
	if (!ktapvm_argv) {
		fprintf(stderr, "canno allocate ktapvm_argv\n");
		return -1;
	}

	ktapvm_argv[0] = malloc(strlen(script_file) + 1);
	if (!ktapvm_argv[0]) {
		fprintf(stderr, "canno allocate memory\n");
		return -1;
	}
	strcpy(ktapvm_argv[0], script_file);
	ktapvm_argv[0][strlen(script_file)] = '\0';

	/* pass rest argv into ktapvm */
	new_index = 1;
	for (i = script_args_start; i < script_args_end; i++) {
		ktapvm_argv[new_index] = malloc(strlen(argv[i]) + 1);
		if (!ktapvm_argv[new_index]) {
			fprintf(stderr, "canno allocate memory\n");
			free(ktapvm_argv);
			return -1;
		}
		strcpy(ktapvm_argv[new_index], argv[i]);
		ktapvm_argv[new_index][strlen(argv[i])] = '\0';
		new_index++;
	}

	uparm.argv = ktapvm_argv;
	uparm.argc = new_index;
	uparm.verbose = verbose;
	uparm.trace_pid = trace_pid;
	uparm.trace_cpu = trace_cpu;
	uparm.print_timestamp = print_timestamp;
	uparm.quiet = quiet;
	uparm.dry_run = dry_run;

	/* start running into kernel ktapvm */
	ret = run_ktapvm();

	cleanup_event_resources();
	return ret;
}


