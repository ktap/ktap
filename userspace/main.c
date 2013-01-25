/*
 * main.c - ktap compiler and loader entry
 *
 * Copyright (C) 2012-2013 Jovi Zhang
 *
 * Author: Jovi Zhang <bookjovi@gmail.com>
 *         zhangwei(Jovi) <jovi.zhangwei@huawei.com>
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
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include "../ktap_types.h"
#include "../ktap_opcodes.h"
#include "ktapc.h"


/*******************************************************************/

void *ktapc_reallocv(void *block, size_t osize, size_t nsize)
{
	return ktap_reallocv(NULL, block, osize, nsize);
}

Closure *ktapc_newlclosure(int n)
{
	return ktap_newlclosure(NULL, n);
}

Proto *ktapc_newproto()
{
	return ktap_newproto(NULL);
}

Tvalue *ktapc_table_set(Table *t, const Tvalue *key)
{
	return table_set(NULL, t, key);
}

Table *ktapc_table_new()
{
	return table_new(NULL);
}

Tstring *ktapc_ts_newlstr(const char *str, size_t l)
{
	return tstring_newlstr(NULL, str, l);
}

Tstring *ktapc_ts_new(const char *str)
{
	return tstring_new(NULL, str);
}

int ktapc_ts_eqstr(Tstring *a, Tstring *b)
{
	return tstring_eqstr(a, b);
}

static void ktapc_runerror(const char *err_msg, const char *what, int limit)
{
	fprintf(stderr, "ktapc_runerror\n");
	fprintf(stderr, err_msg);
	exit(EXIT_FAILURE);
}

/*
 * todo: memory leak here
 */
char *ktapc_sprintf(const char *fmt, ...)
{
	char *msg = malloc(128);

	va_list argp;
	va_start(argp, fmt);
	vsprintf(msg, fmt, argp);
	va_end(argp);
	return msg;
}


#define MINSIZEARRAY	4

void *ktapc_growaux(void *block, int *size, size_t size_elems, int limit,
		    const char *what)
{
	void *newblock;
	int newsize;

	if (*size >= limit/2) {  /* cannot double it? */
		if (*size >= limit)  /* cannot grow even a little? */
			ktapc_runerror("too many %s (limit is %d)", what, limit);
		newsize = limit;  /* still have at least one free place */
	} else {
		newsize = (*size) * 2;
		if (newsize < MINSIZEARRAY)
			newsize = MINSIZEARRAY;  /* minimum size */
	}

	newblock = ktapc_reallocv(block, (*size) * size_elems, newsize * size_elems);
	*size = newsize;  /* update only when everything else is OK */
	return newblock;
}





/*************************************************************************/

#define print_base(i) \
	do {	\
		if (i < f->sizelocvars) /* it's a localvars */ \
			printf("%s", getstr(f->locvars[i].varname));  \
		else \
			printf("base + %d", i);	\
	} while (0)

#define print_RKC(instr)	\
	do {	\
		if (ISK(GETARG_C(instr))) \
			showobj(NULL, k + INDEXK(GETARG_C(instr))); \
		else \
			print_base(GETARG_C(instr)); \
	} while (0)

static void decode_instruction(Proto *f, int instr)
{
	int opcode = GET_OPCODE(instr);
	Tvalue *k;

	k = f->k;

	printf("%.8x\t", instr);
	printf("%s\t", ktap_opnames[opcode]);

	switch (opcode) {
	case OP_GETTABUP:
		print_base(GETARG_A(instr));
		printf(" <- ");

		if (GETARG_B(instr) == 0)
			printf("global");
		else
			printf("upvalues[%d]", GETARG_B(instr));

		printf("{"); print_RKC(instr); printf("}");

		break;
	case OP_GETTABLE:
		print_base(GETARG_A(instr));
		printf(" <- ");

		print_base(GETARG_B(instr));

		printf("{");
		print_RKC(instr);
		printf("}");
		break;
	case OP_LOADK:
		printf("\t");
		print_base(GETARG_A(instr));
		printf(" <- ");

		showobj(NULL, k + GETARG_Bx(instr));
		break;
	case OP_CALL:
		printf("\t");
		print_base(GETARG_A(instr));
		break;
	default:
		break;
	}

	printf("\n");
}

static int function_nr = 0;

/* this is a debug function used for check bytecode chunk file */
static void dump_function(int level, Proto *f)
{
	int i;

	printf("\n----------------------------------------------------\n");
	printf("function %d [level %d]:\n", function_nr++, level);
	printf("linedefined: %d\n", f->linedefined);
	printf("lastlinedefined: %d\n", f->lastlinedefined);
	printf("numparams: %d\n", f->numparams);
	printf("is_vararg: %d\n", f->is_vararg);
	printf("maxstacksize: %d\n", f->maxstacksize);
	printf("source: %s\n", getstr(f->source));
	printf("sizelineinfo: %d \t", f->sizelineinfo);
	for (i = 0; i < f->sizelineinfo; i++)
		printf("%d ", f->lineinfo[i]);
	printf("\n");

	printf("sizek: %d\n", f->sizek);
	for (i = 0; i < f->sizek; i++) {
		switch(f->k[i].type) {
		case KTAP_TNIL:
			printf("\tNIL\n");
			break;
		case KTAP_TBOOLEAN:
			printf("\tBOOLEAN: ");
			printf("%d\n", f->k[i].val.b);
			break;
		case KTAP_TNUMBER:
			printf("\tTNUMBER: ");
			printf("%d\n", f->k[i].val.n);
			break;
		case KTAP_TSTRING:
			printf("\tTSTRING: ");
			printf("%s\n", (Tstring *)f->k[i].val.gc + 1);

			break;
		default:
			printf("\terror: unknow constant type\n");
		}
	}

	printf("sizelocvars: %d\n", f->sizelocvars);
	for (i = 0; i < f->sizelocvars; i++) {
		printf("\tlocvars: %s startpc: %d endpc: %d\n",
			getstr(f->locvars[i].varname), f->locvars[i].startpc,
			f->locvars[i].endpc);
	}

	printf("sizeupvalues: %d\n", f->sizeupvalues);
	for (i = 0; i < f->sizeupvalues; i++) {
		printf("\tname: %s instack: %d idx: %d\n",
			getstr(f->upvalues[i].name), f->upvalues[i].instack,
			f->upvalues[i].idx);
	}

	printf("\n");
	printf("sizecode: %d\n", f->sizecode);
	for (i = 0; i < f->sizecode; i++)
		decode_instruction(f, f->code[i]);

	printf("sizep: %d\n", f->sizep);
	for (i = 0; i < f->sizep; i++)
		dump_function(level + 1, f->p[i]);
	
}

static void usage(const char *msg)
{
	fprintf(stderr, msg);
	fprintf(stderr,
		"usage: ktapc [options] [filenames]\n"
		"Available options are:\n"
		"  -l       list"
		"  -o name  output to file  name default is ktapc.out"
		"  -p       parse only\n"
		"  -s       strip debug information\n"
		"  -v       show version information\n");

	exit(EXIT_FAILURE);
}

global_State dummy_global_state;

static void init_dummy_global_state()
{
	memset(&dummy_global_state, 0, sizeof(global_State));
	dummy_global_state.seed = 201236;

        tstring_resize(NULL, 32); /* set inital string hashtable size */
}


#define handle_error(str) do { perror(str); exit(-1); } while(0)

static const char *default_output_filename = "ktapc.out";

static int ktapc_writer(const void* p, size_t sz, void* ud)
{
	int ret;
	int fdout = (int)ud;

	ret = write(fdout, p, sz);
	if (ret < 0)
		return ret;

	return 0;
}

int main(int argc, char **argv)
{
	Closure *cl;
	unsigned char *buff;
	struct stat sb;
	int fdin, fdout;
        int ktapvm_fd, ktap_fd;

	if (argc == 1)
		usage("miss ktap file argument");

	/* input */
	fdin = open(argv[1], O_RDONLY);
	if (fdin < 0)
		handle_error("open failed");

	if (fstat(fdin, &sb) == -1)
		handle_error("fstat failed");

	buff = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fdin, 0);
	if (buff == MAP_FAILED)
		handle_error("mmap failed");

	init_dummy_global_state();
	cl = ktapc_parser(buff, argv[1]);

	munmap(buff, sb.st_size);
	close(fdin);

	dump_function(1, cl->l.p);
//	return;

	/* ktapc output */
	fdout = open(default_output_filename, O_RDWR | O_CREAT | O_TRUNC, 0);
	if (fdout < 0)
		handle_error("open failed");

	ktapc_dump(cl->l.p, ktapc_writer, fdout, 0);
	close(fdout);

	/* start running into kernel ktapvm */
	ktapvm_fd = open("/dev/ktapvm", O_RDONLY);
	if (ktapvm_fd < 0)
		handle_error("open /dev/ktapvm failed");

	ktap_fd = ioctl(ktapvm_fd, 0, NULL);
	if (ktap_fd < 0)
		handle_error("ioctl ktapvm failed");

	printf("ktaprun: %s\n", argv[1]);
	ioctl(ktap_fd, KTAP_CMD_RUN, default_output_filename);

	close(ktap_fd);
	close(ktapvm_fd);
}

