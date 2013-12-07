/*
 * kp_load.c - loader for ktap bytecode chunk file
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Copyright (C) 1994-2013 Lua.org, PUC-Rio.
 *  - The part of code in this file is copied from lua initially.
 *  - lua's MIT license is compatible with GPL.
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

#include <linux/slab.h>
#include "../include/ktap_types.h"
#include "../include/ktap_ffi.h"
#include "ktap.h"
#include "kp_load.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_tab.h"
#include "kp_vm.h"

#define KTAPC_TAIL	"\x19\x93\r\n\x1a\n"

struct load_state {
	unsigned char *buff;
	int pos;
	ktap_state *ks;
};

#define READ_CHAR(S)  (S->buff[S->pos++])
#define READ_BYTE(S)  READ_CHAR(S)
#define READ_INT(S)  load_int(S)
#define READ_NUMBER(S) load_number(S)
#define READ_STRING(S)	load_string(S)
#define READ_VECTOR(S, dst, size)  \
	do {	\
		memcpy(dst, &S->buff[S->pos], size);	\
		S->pos += size;	\
	} while(0)

#define NEW_VECTOR(S, size)	kp_malloc(S->ks, size)
#define FREE_VECTOR(S, v)	kp_free(S->ks, v)
#define GET_CURRENT(S)		&S->buff[S->pos]
#define ADD_POS(S, size)	S->pos += size


static int load_function(struct load_state *S, ktap_proto *f);


static int load_int(struct load_state *S)
{
	int x;

	READ_VECTOR(S, &x, sizeof(int));
	return x;
}

static long load_number(struct load_state *S)
{
	long x;

	READ_VECTOR(S, &x, sizeof(ktap_number));
	return x;
}

static ktap_string *load_string(struct load_state *S)
{
	ktap_string *ts;
	size_t size;

	size = READ_INT(S);

	if (!size)
		return NULL;
	else {
		char *s = GET_CURRENT(S);
		ADD_POS(S, size);
		/* remove trailing '\0' */
		ts = kp_tstring_newlstr(S->ks, s, size - 1);
		return ts;
	}
}


static int load_code(struct load_state *S, ktap_proto *f)
{
	int n = READ_INT(S);

	f->sizecode = n;
	f->code = NEW_VECTOR(S, n * sizeof(ktap_instruction));
	READ_VECTOR(S, f->code, n * sizeof(ktap_instruction));

	return 0;
}

static int load_constants(struct load_state *S, ktap_proto *f)
{
	int i,n;

	n = READ_INT(S);

	f->sizek = n;
	f->k = NEW_VECTOR(S, n * sizeof(ktap_value));
	for (i = 0; i < n; i++)
		set_nil(&f->k[i]);

	for (i=0; i < n; i++) {
		ktap_value *o = &f->k[i];

		int t = READ_CHAR(S);
		switch (t) {
		case KTAP_TNIL:
			set_nil(o);
			break;
		case KTAP_TBOOLEAN:
			set_boolean(o, READ_CHAR(S));
			break;
		case KTAP_TNUMBER:
			/*
			 * todo: kernel not support fp, check double when
			 * loading
			 */
			set_number(o, READ_NUMBER(S));
			break;
		case KTAP_TSTRING:
			set_string(o, READ_STRING(S));
			break;
		default:
			kp_error(S->ks, "ktap: load_constants: "
					"unknow ktap_value\n");
			return -1;

		}
	}

	n = READ_INT(S);
	f->p = NEW_VECTOR(S, n * sizeof(ktap_proto));
	f->sizep = n;
	for (i = 0; i < n; i++)
		f->p[i] = NULL;
	for (i = 0; i < n; i++) {
		f->p[i] = kp_newproto(S->ks);
		if (load_function(S, f->p[i]))
			return -1;
	}

	return 0;
}


static int load_upvalues(struct load_state *S, ktap_proto *f)
{
	int i,n;

	n = READ_INT(S);
	f->upvalues = NEW_VECTOR(S, n * sizeof(ktap_upvaldesc));
	f->sizeupvalues = n;

	for (i = 0; i < n; i++)
		f->upvalues[i].name = NULL;

	for (i = 0; i < n; i++) {
		f->upvalues[i].instack = READ_BYTE(S);
		f->upvalues[i].idx = READ_BYTE(S);
	}

	return 0;
}

static int load_debuginfo(struct load_state *S, ktap_proto *f)
{
	int i,n;

	f->source = READ_STRING(S);
	n = READ_INT(S);
	f->sizelineinfo = n;
	f->lineinfo = NEW_VECTOR(S, n * sizeof(int));
	READ_VECTOR(S, f->lineinfo, n * sizeof(int));
	n = READ_INT(S);
	f->locvars = NEW_VECTOR(S, n * sizeof(struct ktap_locvar));
	f->sizelocvars = n;
	for (i = 0; i < n; i++)
		f->locvars[i].varname = NULL;
	for (i = 0; i < n; i++) {
		f->locvars[i].varname = READ_STRING(S);
		f->locvars[i].startpc = READ_INT(S);
		f->locvars[i].endpc = READ_INT(S);
	}
	n = READ_INT(S);
	for (i = 0; i < n; i++)
		f->upvalues[i].name = READ_STRING(S);

	return 0;
}

static int load_function(struct load_state *S, ktap_proto *f)
{
	f->linedefined = READ_INT(S);
 	f->lastlinedefined = READ_INT(S);
	f->numparams = READ_BYTE(S);
	f->is_vararg = READ_BYTE(S);
	f->maxstacksize = READ_BYTE(S);
	if (load_code(S, f))
		return -1;
	if (load_constants(S, f))
		return -1;
	if (load_upvalues(S, f))
		return -1;
	if (load_debuginfo(S, f))
		return -1;

	return 0;
}


#define error(S, why) \
	kp_error(S->ks, "load failed: %s precompiled chunk\n", why)

#define N0	KTAPC_HEADERSIZE
#define N1	(sizeof(KTAP_SIGNATURE) - sizeof(char))
#define N2	N1 + 2
#define N3	N2 + 6

static int load_header(struct load_state *S)
{
	u8 h[KTAPC_HEADERSIZE];
	u8 s[KTAPC_HEADERSIZE];

	kp_header(h);
	READ_VECTOR(S, s, KTAPC_HEADERSIZE);

	if (memcmp(h, s, N0) == 0)
		return 0;
	if (memcmp(h, s, N1) != 0)
		error(S, "not a");
	else if (memcmp(h, s, N2) != 0)
		error(S, "version mismatch in");
	else if (memcmp(h, s, N3) != 0)
		error(S, "incompatible");
	else
		error(S,"corrupted");

	return -1;
}

#ifdef CONFIG_KTAP_FFI
void ffi_set_csym_arr(ktap_state *ks, int cs_nr, csymbol *new_arr);

static void load_csymbol_func(struct load_state *S, csymbol *cs)
{
	csymbol_func *csf = csym_func(cs);
	int arg_nr = csymf_arg_nr(csf);

	if (arg_nr > 0) {
		csf->arg_ids = NEW_VECTOR(S, arg_nr*sizeof(int));
		READ_VECTOR(S, csf->arg_ids, arg_nr*sizeof(int));
	} else {
		csf->arg_ids = NULL;
	}
}

static void load_csymbol_struct(struct load_state *S, csymbol *cs)
{
	csymbol_struct *csst = csym_struct(cs);
	int mb_nr = csymst_mb_nr(csst);

	csst->members = NEW_VECTOR(S, mb_nr*sizeof(struct_member));
	READ_VECTOR(S, csst->members, mb_nr*sizeof(struct_member));
}

static int load_csymbols(struct load_state *S)
{
	csymbol *cs_arr, *cs;
	int i, csym_nr;

	/* read number of csymbols */
	csym_nr = READ_INT(S);
	if (csym_nr <= 0) {
		ffi_set_csym_arr(S->ks, 0, NULL);
		return 0;
	}

	/* csymbol size safty check */
	if (sizeof(csymbol) != READ_INT(S)) {
		kp_error(S->ks, "invalid csymbol size in chunk\n");
		return -1;
	}

	cs_arr = NEW_VECTOR(S, sizeof(csymbol)*csym_nr);
	for (i = 0; i < csym_nr; i++) {
		cs = &cs_arr[i];
		READ_VECTOR(S, cs, sizeof(csymbol));
		switch (cs->type) {
		case FFI_FUNC:
			load_csymbol_func(S, cs);
			break;
		case FFI_STRUCT:
			load_csymbol_struct(S, cs);
			break;
		default:
			break;
		}
	}

	ffi_set_csym_arr(S->ks, csym_nr, cs_arr);

	return 0;
}
#else
static int load_csymbols(struct load_state *S)
{
	int csym_nr = READ_INT(S);

	/* if FFI is disabled in ktapc, csym_nr should be 0 */
	if (csym_nr != 0) {
		 /* skip corrupted csymbol chunk */
		int cs_size = READ_INT(S);
		ADD_POS(S, cs_size*csym_nr);
		kp_error(S->ks, "VM compiled without FFI support!\n");
		return -1;
	}

	return 0;
}
#endif

static int verify_code(struct load_state *S, ktap_proto *f)
{
	/* not support now */
	return 0;
}


ktap_closure *kp_load(ktap_state *ks, unsigned char *buff)
{
	struct load_state S;
	ktap_closure *cl;
	int ret, i;

	S.ks = ks;
	S.buff = buff;
	S.pos = 0;

	ret = load_header(&S);
	if (ret)
		return NULL;

	ret = load_csymbols(&S);
	if (ret)
		return NULL;

	cl = kp_newclosure(ks, 1);
	if (!cl)
		return cl;

	/* put closure on the top, prepare to run with this closure */
	set_closure(ks->top, cl);
	incr_top(ks);

	cl->p = kp_newproto(ks);
	if (load_function(&S, cl->p))
		return NULL;

	if (cl->p->sizeupvalues != 1) {
		ktap_proto *p = cl->p;
		cl = kp_newclosure(ks, cl->p->sizeupvalues);
		cl->p = p;
		set_closure(ks->top - 1, cl);
	}

	for (i = 0; i < cl->nupvalues; i++) {  /* initialize upvalues */
		ktap_upval *up = kp_newupval(ks);
		cl->upvals[i] = up;
	}

	/* set global table as 1st upvalue of 'f' */
	if (cl->nupvalues == 1) {
		ktap_tab *reg = hvalue(&G(ks)->registry);
		const ktap_value *gt = kp_tab_getint(reg, KTAP_RIDX_GLOBALS);
		set_obj(cl->upvals[0]->v, gt);
	}

	verify_code(&S, cl->p);

	return cl;
}

