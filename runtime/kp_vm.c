/*
 * kp_vm.c - ktap script virtual machine in Linux kernel
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
#include <linux/ftrace_event.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include "../include/ktap_types.h"
#include "../include/ktap_opcodes.h"
#include "../include/ktap_ffi.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_tab.h"
#include "kp_transport.h"
#include "kp_vm.h"

#define KTAP_MIN_RESERVED_STACK_SIZE 20
#define KTAP_STACK_SIZE		120 /* enlarge this value for big stack */
#define KTAP_STACK_SIZE_BYTES	(KTAP_STACK_SIZE * sizeof(ktap_value))

#define CIST_KTAP	(1 << 0) /* call is running a ktap function */
#define CIST_REENTRY	(1 << 2)

#define isktapfunc(ci)	((ci)->callstatus & CIST_KTAP)


/* common helper function */
long gettimeofday_ns(void)
{
	struct timespec now;

	getnstimeofday(&now);
	return now.tv_sec * NSEC_PER_SEC + now.tv_nsec;
}


static void ktap_concat(ktap_state *ks, int start, int end)
{
	int i, len = 0;
	StkId top = ks->ci->u.l.base;
	ktap_string *ts;
	char *ptr, *buffer;

	for (i = start; i <= end; i++) {
		if (!is_string(top + i)) {
			kp_error(ks, "cannot concat non-string\n");
			set_nil(top + start);
			return;
		}

		len += rawtsvalue(top + i)->len;
	}

	if (len >= KTAP_PERCPU_BUFFER_SIZE) {
		kp_error(ks, "Error: too long string concatenation\n");
		return;
	}

	preempt_disable_notrace();

	buffer = kp_percpu_data(ks, KTAP_PERCPU_DATA_BUFFER);
	ptr = buffer;

	for (i = start; i <= end; i++) {
		int len = rawtsvalue(top + i)->len;
		strncpy(ptr, svalue(top + i), len);
		ptr += len;
	}
	ts = kp_str_newlstr(ks, buffer, len);
	set_string(top + start, ts);

	preempt_enable_notrace();
}

/* todo: compare l == r if both is tstring type? */
static int lessthan(ktap_state *ks, const ktap_value *l, const ktap_value *r)
{
	if (is_number(l) && is_number(r))
		return NUMLT(nvalue(l), nvalue(r));
	else if (is_string(l) && is_string(r))
		return kp_str_cmp(rawtsvalue(l), rawtsvalue(r)) < 0;

	return 0;
}

static int lessequal(ktap_state *ks, const ktap_value *l, const ktap_value *r)
{
	if (is_number(l) && is_number(r))
		return NUMLE(nvalue(l), nvalue(r));
	else if (is_string(l) && is_string(r))
		return kp_str_cmp(rawtsvalue(l), rawtsvalue(r)) <= 0;

	return 0;
}

static const ktap_value *ktap_tonumber(const ktap_value *obj, ktap_value *n)
{
	if (is_number(obj))
		return obj;

	return NULL;
}

static ktap_upval *findupval(ktap_state *ks, StkId level)
{
	ktap_global_state *g = G(ks);
	ktap_gcobject **pp = &ks->openupval;
	ktap_upval *p;
	ktap_upval *uv;

	while (*pp != NULL && (p = gco2uv(*pp))->v >= level) {
		if (p->v == level) {  /* found a corresponding upvalue? */
			return p;
		}
		pp = &p->next;
	}

	/* not found: create a new one */
	uv = &kp_obj_newobject(ks, KTAP_TYPE_UPVAL, sizeof(ktap_upval), pp)->uv;
	uv->v = level;  /* current value lives in the stack */
	uv->u.l.prev = &g->uvhead;  /* double link it in `uvhead' list */
	uv->u.l.next = g->uvhead.u.l.next;
	uv->u.l.next->u.l.prev = uv;
	g->uvhead.u.l.next = uv;
	return uv;
}

static void unlinkupval(ktap_upval *uv)
{
	uv->u.l.next->u.l.prev = uv->u.l.prev;  /* remove from `uvhead' list */
	uv->u.l.prev->u.l.next = uv->u.l.next;
}

void kp_freeupval(ktap_state *ks, ktap_upval *uv)
{
	if (uv->v != &uv->u.value)  /* is it open? */
		unlinkupval(uv);  /* remove from open list */
	kp_free(ks, uv);  /* free upvalue */
}

/* close upvals */
static void function_close(ktap_state *ks, StkId level)
{
	ktap_upval *uv;
	ktap_global_state *g = G(ks);
	while (ks->openupval != NULL &&
		(uv = gco2uv(ks->openupval))->v >= level) {
		ktap_gcobject *o = obj2gco(uv);
		ks->openupval = uv->next;  /* remove from `open' list */
		unlinkupval(uv);  /* remove upvalue from 'uvhead' list */
		set_obj(&uv->u.value, uv->v);  /* move value to upvalue slot */
		uv->v = &uv->u.value;  /* now current value lives here */
		gch(o)->next = g->allgc;  /* link upvalue into 'allgc' list */
		g->allgc = o;
	}
}

/* create a new closure */
static void pushclosure(ktap_state *ks, ktap_proto *p, ktap_upval **encup,
			StkId base, StkId ra)
{
	int nup = p->sizeupvalues;
	ktap_upvaldesc *uv = p->upvalues;
	int i;
	ktap_closure *cl = kp_obj_newclosure(ks, nup);

	cl->p = p;
	set_closure(ra, cl);  /* anchor new closure in stack */

	/* fill in its upvalues */
	for (i = 0; i < nup; i++) {
		if (uv[i].instack) {
			/* upvalue refers to local variable? */
			cl->upvals[i] = findupval(ks, base + uv[i].idx);
		} else {
			/* get upvalue from enclosing function */
			cl->upvals[i] = encup[uv[i].idx];
		}
	}
	//p->cache = ncl;  /* save it on cache for reuse */
}

/* arg t and val may point to the same address */
static void gettable(ktap_state *ks, const ktap_value *t, ktap_value *key,
		     StkId val)
{
	if (is_table(t)) {
		set_obj(val, kp_tab_get(hvalue(t), key));
	} else if (is_ptable(t)) {
		kp_ptab_get(ks, phvalue(t), key, val);
#ifdef CONFIG_KTAP_FFI
	} else if (is_cdata(t) && gcvalue(t) != NULL
			&& cd_type(ks, cdvalue(t)) == FFI_PTR) {
		kp_cdata_ptr_get(ks, cdvalue(t), key, val);
	} else if (is_cdata(t) && gcvalue(t) != NULL
			&& (cd_type(ks, cdvalue(t)) == FFI_STRUCT
			|| cd_type(ks, cdvalue(t)) == FFI_UNION)) {
		kp_cdata_record_get(ks, cdvalue(t), key, val);
#endif
	} else {
		kp_error(ks, "get key from non-table\n");
	}
}

static void settable(ktap_state *ks, const ktap_value *t, ktap_value *key,
		     StkId val)
{
	if (is_table(t)) {
		kp_tab_setvalue(ks, hvalue(t), key, val);
	} else if (is_ptable(t)) {
		kp_ptab_set(ks, phvalue(t), key, val);
#ifdef CONFIG_KTAP_FFI
	} else if (is_cdata(t) && gcvalue(t) != NULL
			&& cd_type(ks, cdvalue(t)) == FFI_PTR) {
		kp_cdata_ptr_set(ks, cdvalue(t), key, val);
	} else if (is_cdata(t) && gcvalue(t) != NULL
			&& (cd_type(ks, cdvalue(t)) == FFI_STRUCT
			|| cd_type(ks, cdvalue(t)) == FFI_UNION)) {
		kp_cdata_record_set(ks, cdvalue(t), key, val);
#endif
	} else {
		kp_error(ks, "set key to non-table\n");
	}
}

static void settable_incr(ktap_state *ks, const ktap_value *t, ktap_value *key,
			  StkId val)
{
	if (unlikely(!is_table(t))) {
		kp_error(ks, "use += operator for non-table\n");
		return;
	}

	if (unlikely(!is_number(val))) {
		kp_error(ks, "use non-number to += operator\n");
		return;
	}

	kp_tab_atomic_inc(ks, hvalue(t), key, nvalue(val));
}

static inline int checkstack(ktap_state *ks, int n)
{
	if (unlikely(ks->stack_last - ks->top <= n)) {
		kp_error(ks, "stack overflow, please enlarge stack size\n");
		return -1;
	}

	return 0;
}

static StkId adjust_varargs(ktap_state *ks, ktap_proto *p, int actual)
{
	int i;
	int nfixargs = p->numparams;
	StkId base, fixed;

	/* move fixed parameters to final position */
	fixed = ks->top - actual;  /* first fixed argument */
	base = ks->top;  /* final position of first argument */

	for (i=0; i < nfixargs; i++) {
		set_obj(ks->top++, fixed + i);
		set_nil(fixed + i);
	}

	return base;
}

static int poscall(ktap_state *ks, StkId first_result)
{
	ktap_callinfo *ci;
	StkId res;
	int wanted, i;

	ci = ks->ci;

	res = ci->func;
	wanted = ci->nresults;

	ks->ci = ci = ci->prev;

	for (i = wanted; i != 0 && first_result < ks->top; i--)
		set_obj(res++, first_result++);

	while(i-- > 0)
		set_nil(res++);

	ks->top = res;

	return (wanted - (-1));
}

static ktap_callinfo *extend_ci(ktap_state *ks)
{
	ktap_callinfo *ci;

	ci = kp_malloc(ks, sizeof(ktap_callinfo));
	ks->ci->next = ci;
	ci->prev = ks->ci;
	ci->next = NULL;

	return ci;
}

static void free_ci(ktap_state *ks)
{
	ktap_callinfo *ci = ks->ci;
	ktap_callinfo *next;

	if (!ci)
		return;

	next = ci->next;
	ci->next = NULL;
	while ((ci = next) != NULL) {
		next = ci->next;
		kp_free(ks, ci);
	}
}

#define next_ci(ks) (ks->ci = ks->ci->next ? ks->ci->next : extend_ci(ks))
#define savestack(ks, p)	((char *)(p) - (char *)ks->stack)
#define restorestack(ks, n)	((ktap_value *)((char *)ks->stack + (n)))

static int precall(ktap_state *ks, StkId func, int nresults)
{
	ktap_cfunction f;
	ktap_callinfo *ci;
	ktap_proto *p;
#ifdef CONFIG_KTAP_FFI
	ktap_cdata *cd;
	csymbol *cs;
#endif
	StkId base;
	ptrdiff_t funcr = savestack(ks, func);
	int n;

	switch (ttype(func)) {
	case KTAP_TYPE_CFUNCTION: /* light C function */
		f = fvalue(func);

		if (checkstack(ks, KTAP_MIN_RESERVED_STACK_SIZE))
			return 1;

		ci = next_ci(ks);
		ci->nresults = nresults;
		ci->func = restorestack(ks, funcr);
		ci->top = ks->top + KTAP_MIN_RESERVED_STACK_SIZE;
		ci->callstatus = 0;
		n = (*f)(ks);
		poscall(ks, ks->top - n);
		return 1;
	case KTAP_TYPE_CLOSURE:
		p = clvalue(func)->p;

		if (checkstack(ks, p->maxstacksize))
			return 1;

		func = restorestack(ks, funcr);
		n = (int)(ks->top - func) - 1; /* number of real arguments */

		/* complete missing arguments */
		for (; n < p->numparams; n++)
			set_nil(ks->top++);

		base = (!p->is_vararg) ? func + 1 : adjust_varargs(ks, p, n);
		ci = next_ci(ks);
		ci->nresults = nresults;
		ci->func = func;
		ci->u.l.base = base;
		ci->top = base + p->maxstacksize;
		ci->u.l.savedpc = p->code; /* starting point */
		ci->callstatus = CIST_KTAP;
		ks->top = ci->top;
		return 0;
#ifdef CONFIG_KTAP_FFI
	case KTAP_TYPE_CDATA:
		cd = cdvalue(func);

		if (checkstack(ks, KTAP_MIN_RESERVED_STACK_SIZE))
			return 1;

		if (cd_type(ks, cd) != FFI_FUNC)
			kp_error(ks, "Value in cdata is not a c funcion\n");
		cs = cd_csym(ks, cd);
		kp_verbose_printf(ks, "calling ffi function [%s] with address %p\n",
				csym_name(cs), csym_func_addr(cs));

		ci = next_ci(ks);
		ci->nresults = nresults;
		ci->func = restorestack(ks, funcr);
		ci->top = ks->top + KTAP_MIN_RESERVED_STACK_SIZE;
		ci->callstatus = 0;

		n = ffi_call(ks, csym_func(cs));
		kp_verbose_printf(ks, "returned from ffi call...\n");
		poscall(ks, ks->top - n);
		return 1;
#endif
	default:
		kp_error(ks, "attempt to call nil function\n");
	}

	return 0;
}

#define RA(i)   (base+GETARG_A(i))
#define RB(i)   (base+GETARG_B(i))
#define ISK(x)  ((x) & BITRK)
#define RC(i)   base+GETARG_C(i)
#define RKB(i) \
        ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i)
#define RKC(i)  \
        ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i)

#define dojump(ci,i,e) { \
	ci->u.l.savedpc += GETARG_sBx(i) + e; }
#define donextjump(ci)  { instr = *ci->u.l.savedpc; dojump(ci, instr, 1); }

#define arith_op(ks, op) { \
	ktap_value *rb = RKB(instr); \
	ktap_value *rc = RKC(instr); \
	if (is_number(rb) && is_number(rc)) { \
		ktap_number nb = nvalue(rb), nc = nvalue(rc); \
		set_number(ra, op(nb, nc)); \
	} else {	\
		kp_puts(ks, "Error: Cannot make arith operation\n");	\
		return;	\
	} }

static ktap_value *cfunction_cache_get(ktap_state *ks, int index);

static void ktap_execute(ktap_state *ks)
{
	int exec_count = 0;
	ktap_callinfo *ci;
	ktap_closure *cl;
	ktap_value *k;
	unsigned int instr, opcode;
	StkId base; /* stack pointer */
	StkId ra; /* register pointer */
	int res, nresults; /* temp varible */

	ci = ks->ci;

 newframe:
	cl = clvalue(ci->func);
	k = cl->p->k;
	base = ci->u.l.base;

 mainloop:
	/* main loop of interpreter */

	/* dead loop detaction */
	if (exec_count++ == kp_max_exec_count) {
		if (G(ks)->mainthread != ks) {
			kp_error(ks, "non-mainthread executed instructions "
				     "exceed max limit(%d)\n",
					kp_max_exec_count);
			return;
		}

		cond_resched();

		if (G(ks)->exit)
			return;

		if (signal_pending(current)) {
			flush_signals(current);
			return;
		}
		exec_count = 0;
	}

	instr = *(ci->u.l.savedpc++);
	opcode = GET_OPCODE(instr);

	/* ra is target register */
	ra = RA(instr);

	switch (opcode) {
	case OP_MOVE:
		set_obj(ra, base + GETARG_B(instr));
		break;
	case OP_LOADK:
		set_obj(ra, k + GETARG_Bx(instr));
		break;
	case OP_LOADKX:
		set_obj(ra, k + GETARG_Ax(*ci->u.l.savedpc++));
		break;
	case OP_LOADBOOL:
		set_boolean(ra, GETARG_B(instr));
		if (GETARG_C(instr))
			ci->u.l.savedpc++;
		break;
	case OP_LOADNIL: {
		int b = GETARG_B(instr);
		do {
			set_nil(ra++);
		} while (b--);
		break;
		}
	case OP_GETUPVAL: {
		int b = GETARG_B(instr);
		set_obj(ra, cl->upvals[b]->v);
		break;
		}
	case OP_GETTABUP: {
		int b = GETARG_B(instr);
		gettable(ks, cl->upvals[b]->v, RKC(instr), ra);
		base = ci->u.l.base;
		break;
		}
	case OP_GETTABLE:
		gettable(ks, RB(instr), RKC(instr), ra);
		base = ci->u.l.base;
		break;
	case OP_SETTABUP: {
		int a = GETARG_A(instr);
		settable(ks, cl->upvals[a]->v, RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
		}
	case OP_SETTABUP_INCR: {
		int a = GETARG_A(instr);
		settable_incr(ks, cl->upvals[a]->v, RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
		}
	case OP_SETTABUP_AGGR: {
		int a = GETARG_A(instr);
		ktap_value *v = cl->upvals[a]->v;
		if (!is_ptable(v)) {
			kp_error(ks, "<<< must be operate on ptable\n");
			return;
		}

		kp_ptab_set(ks, phvalue(v), RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
		}
	case OP_SETUPVAL: {
		ktap_upval *uv = cl->upvals[GETARG_B(instr)];
		set_obj(uv->v, ra);
		break;
		}
	case OP_SETTABLE:
		settable(ks, ra, RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
	case OP_SETTABLE_INCR:
		settable_incr(ks, ra, RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
	case OP_SETTABLE_AGGR:
		if (!is_ptable(ra)) {
			kp_error(ks, "<<< must be operate on ptable\n");
			return;
		}

		kp_ptab_set(ks, phvalue(ra), RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
	case OP_NEWTABLE: {
		/* 
		 * preallocate default narr and nrec,
		 * ARG_B and ARG_C is not used
		 * This would allocate more memory for some static table.
		 */
		ktap_tab *t = kp_tab_new(ks, 0, 0);
		if (unlikely(!t))
			return;
		set_table(ra, t);
		break;
		}
	case OP_SELF: {
		StkId rb = RB(instr);
		set_obj(ra+1, rb);
		gettable(ks, rb, RKC(instr), ra);
		base = ci->u.l.base;
		break;
		}
	case OP_ADD:
		arith_op(ks, NUMADD);
		break;
	case OP_SUB:
		arith_op(ks, NUMSUB);
		break;
	case OP_MUL:
		arith_op(ks, NUMMUL);
		break;
	case OP_DIV:
		/* divide 0 checking */
		if (!nvalue(RKC(instr))) {
			kp_error(ks, "divide 0 arith operation\n");
			return;
		}
		arith_op(ks, NUMDIV);
		break;
	case OP_MOD:
		/* divide 0 checking */
		if (!nvalue(RKC(instr))) {
			kp_error(ks, "mod 0 arith operation\n");
			return;
		}
		arith_op(ks, NUMMOD);
		break;
	case OP_POW:
		kp_error(ks, "ktap don't support pow arith in kernel\n");
		return;
	case OP_UNM: {
		ktap_value *rb = RB(instr);
		if (is_number(rb)) {
			ktap_number nb = nvalue(rb);
			set_number(ra, NUMUNM(nb));
		}
		break;
		}
	case OP_NOT:
		res = is_false(RB(instr));
		set_boolean(ra, res);
		break;
	case OP_LEN: {
		int len = kp_obj_len(ks, RB(instr));
		if (len < 0)
			return;
		set_number(ra, len);
		break;
		}
	case OP_CONCAT: {
		int b = GETARG_B(instr);
		int c = GETARG_C(instr);
		ktap_concat(ks, b, c);
		break;
		}
	case OP_JMP:
		dojump(ci, instr, 0);
		break;
	case OP_EQ: {
		ktap_value *rb = RKB(instr);
		ktap_value *rc = RKC(instr);
		if ((int)rawequalobj(rb, rc) != GETARG_A(instr))
			ci->u.l.savedpc++;
		else
			donextjump(ci);

		base = ci->u.l.base;
		break;
		}
	case OP_LT: {
		if (lessthan(ks, RKB(instr), RKC(instr)) != GETARG_A(instr)) {
			ci->u.l.savedpc++;
		} else
			donextjump(ci);
		base = ci->u.l.base;
		break;
		}
	case OP_LE:
		if (lessequal(ks, RKB(instr), RKC(instr)) != GETARG_A(instr))
			ci->u.l.savedpc++;
		else
			donextjump(ci);
		base = ci->u.l.base;
		break;
	case OP_TEST:
		if (GETARG_C(instr) ? is_false(ra) : !is_false(ra))
			ci->u.l.savedpc++;
		else
			donextjump(ci);
		break;
	case OP_TESTSET: {
		ktap_value *rb = RB(instr);
		if (GETARG_C(instr) ? is_false(rb) : !is_false(rb))
			ci->u.l.savedpc++;
		else {
			set_obj(ra, rb);
			donextjump(ci);
		}
		break;
		}
	case OP_CALL: {
		int b = GETARG_B(instr);
		int ret;

		nresults = GETARG_C(instr) - 1;

		if (b != 0)
			ks->top = ra + b;

		ret = precall(ks, ra, nresults);
		if (ret) { /* C function */
			if (nresults >= 0)
				ks->top = ci->top;
			base = ci->u.l.base;
			break;
		} else { /* ktap function */
			ci = ks->ci;
			/* this flag is used for return time, see OP_RETURN */
			ci->callstatus |= CIST_REENTRY;
			goto newframe;
		}
		break;
		}
	case OP_TAILCALL: {
		int b = GETARG_B(instr);

		if (b != 0)
			ks->top = ra+b;
		if (precall(ks, ra, -1))  /* C function? */
			base = ci->u.l.base;
		else {
			int aux;

			/*
			 * tail call: put called frame (n) in place of
			 * caller one (o)
			 */
			ktap_callinfo *nci = ks->ci;  /* called frame */
			ktap_callinfo *oci = nci->prev;  /* caller frame */
			StkId nfunc = nci->func;  /* called function */
			StkId ofunc = oci->func;  /* caller function */
			/* last stack slot filled by 'precall' */
			StkId lim = nci->u.l.base +
				    clvalue(nfunc)->p->numparams;

			/* close all upvalues from previous call */
			if (cl->p->sizep > 0)
				function_close(ks, oci->u.l.base);

			/* move new frame into old one */
			for (aux = 0; nfunc + aux < lim; aux++)
				set_obj(ofunc + aux, nfunc + aux);
			/* correct base */
			oci->u.l.base = ofunc + (nci->u.l.base - nfunc);
			/* correct top */
			oci->top = ks->top = ofunc + (ks->top - nfunc);
			oci->u.l.savedpc = nci->u.l.savedpc;
			/* remove new frame */
			ci = ks->ci = oci;
			/* restart ktap_execute over new ktap function */
			goto newframe;
		}
		break;
		}
	case OP_RETURN: {
		int b = GETARG_B(instr);
		if (b != 0)
			ks->top = ra+b-1;
		if (cl->p->sizep > 0)
			function_close(ks, base);
		b = poscall(ks, ra);

		/* if it's called from external invocation, just return */
		if (!(ci->callstatus & CIST_REENTRY))
			return;

		ci = ks->ci;
		if (b)
			ks->top = ci->top;
		goto newframe;
		}
	case OP_FORLOOP: {
		ktap_number step = nvalue(ra+2);
		/* increment index */
		ktap_number idx = NUMADD(nvalue(ra), step);
		ktap_number limit = nvalue(ra+1);
		if (NUMLT(0, step) ? NUMLE(idx, limit) : NUMLE(limit, idx)) {
			ci->u.l.savedpc += GETARG_sBx(instr);  /* jump back */
			set_number(ra, idx);  /* update internal index... */
			set_number(ra+3, idx);  /* ...and external index */
		}
		break;
		}
	case OP_FORPREP: {
		const ktap_value *init = ra;
		const ktap_value *plimit = ra + 1;
		const ktap_value *pstep = ra + 2;

		if (!ktap_tonumber(init, ra)) {
			kp_error(ks, KTAP_QL("for")
				 " initial value must be a number\n");
			return;
		} else if (!ktap_tonumber(plimit, ra + 1)) {
			kp_error(ks, KTAP_QL("for")
				 " limit must be a number\n");
			return;
		} else if (!ktap_tonumber(pstep, ra + 2)) {
			kp_error(ks, KTAP_QL("for") " step must be a number\n");
			return;
		}

		set_number(ra, NUMSUB(nvalue(ra), nvalue(pstep)));
		ci->u.l.savedpc += GETARG_sBx(instr);
		break;
		}
	case OP_TFORCALL: {
		StkId cb = ra + 3;  /* call base */
		set_obj(cb + 2, ra + 2);
		set_obj(cb + 1, ra + 1);
		set_obj(cb, ra);
		ks->top = cb + 3;  /* func. + 2 args (state and index) */
		kp_call(ks, cb, GETARG_C(instr));
		base = ci->u.l.base;
		ks->top = ci->top;
		instr = *(ci->u.l.savedpc++);  /* go to next instruction */
		ra = RA(instr);
		}
		/*go through */
	case OP_TFORLOOP:
		if (!is_nil(ra + 1)) {  /* continue loop? */
			set_obj(ra, ra + 1);  /* save control variable */
			ci->u.l.savedpc += GETARG_sBx(instr);  /* jump back */
		}
		break;
	case OP_SETLIST: {
		int n = GETARG_B(instr);
		int c = GETARG_C(instr);
		int last;
		ktap_tab *h;

		if (n == 0)
			n = (int)(ks->top - ra) - 1;
		if (c == 0)
			c = GETARG_Ax(*ci->u.l.savedpc++);

		h = hvalue(ra);
		last = ((c - 1) * LFIELDS_PER_FLUSH) + n;

		for (; n > 0; n--) {
			ktap_value *val = ra+n;
			kp_tab_setint(ks, h, last--, val);
		}
		/* correct top (in case of previous open call) */
		ks->top = ci->top;
		break;
		}
	case OP_CLOSURE: {
		/* need to use closure cache? (multithread contention issue)*/
		ktap_proto *p = cl->p->p[GETARG_Bx(instr)];
		pushclosure(ks, p, cl->upvals, base, ra);
		break;
		}
	case OP_VARARG: {
		int b = GETARG_B(instr) - 1;
		int j;
		int n = (int)(base - ci->func) - cl->p->numparams - 1;
		if (b < 0) {  /* B == 0? */
			b = n;  /* get all var. arguments */
			if(checkstack(ks, n))
				return;
			/* previous call may change the stack */
			ra = RA(instr);
			ks->top = ra + n;
		}
		for (j = 0; j < b; j++) {
			if (j < n) {
				set_obj(ra + j, base - n + j);
			} else
				set_nil(ra + j);
		}
		break;
		}
	case OP_EXTRAARG:
		return;

	case OP_EVENT: {
		struct ktap_event *e = ks->current_event;

		if (unlikely(!e)) {
			kp_error(ks, "invalid event context\n");
			return;
		}
		set_event(ra, e);
		break;
		}

	case OP_EVENTNAME: {
		struct ktap_event *e = ks->current_event;

		if (unlikely(!e)) {
			kp_error(ks, "invalid event context\n");
			return;
		}
		set_string(ra, kp_str_new(ks, e->call->name));
		break;
		}
	case OP_EVENTARG:
		if (unlikely(!ks->current_event)) {
			kp_error(ks, "invalid event context\n");
			return;
		}

		kp_event_getarg(ks, ra, GETARG_B(instr));
		break;
	case OP_LOAD_GLOBAL: {
		ktap_value *cfunc = cfunction_cache_get(ks, GETARG_C(instr));
		set_obj(ra, cfunc);
		}
		break;

	case OP_EXIT:
		return;
	}

	goto mainloop;
}

void kp_call(ktap_state *ks, StkId func, int nresults)
{
	if (!precall(ks, func, nresults))
		ktap_execute(ks);
}

static int cfunction_cache_getindex(ktap_state *ks, ktap_value *fname);

/*
 * This function must be called before all code loaded.
 */
void kp_optimize_code(ktap_state *ks, int level, ktap_proto *f)
{
	int i;

	for (i = 0; i < f->sizecode; i++) {
		int instr = f->code[i];
		ktap_value *k = f->k;

		if (GET_OPCODE(instr) == OP_GETTABUP) {
			if ((GETARG_B(instr) == 0) && ISK(GETARG_C(instr))) {
				ktap_value *field = k + INDEXK(GETARG_C(instr));
				if (ttype(field) == KTAP_TYPE_STRING) {
					int index = cfunction_cache_getindex(ks,
									field);
					if (index == -1)
						break;

					SET_OPCODE(instr, OP_LOAD_GLOBAL);
					SETARG_C(instr, index);
					f->code[i] = instr;
					break;
				}
			}
		}
	}

	/* continue optimize sub functions */
	for (i = 0; i < f->sizep; i++)
		kp_optimize_code(ks, level + 1, f->p[i]);
}

static ktap_value *cfunction_cache_get(ktap_state *ks, int index)
{
	return &G(ks)->cfunction_tbl[index];
}

static int cfunction_cache_getindex(ktap_state *ks, ktap_value *fname)
{
	const ktap_value *gt = kp_tab_getint(hvalue(&G(ks)->registry),
				KTAP_RIDX_GLOBALS);
	const ktap_value *cfunc;
	int nr, i;

	nr = G(ks)->nr_builtin_cfunction;
	cfunc = kp_tab_get(hvalue(gt), fname);

	for (i = 0; i < nr; i++) {
		if (rawequalobj(&G(ks)->cfunction_tbl[i], cfunc))
			return i;
	}

	return -1;
}

static void cfunction_cache_add(ktap_state *ks, ktap_value *func)
{
	int nr = G(ks)->nr_builtin_cfunction;
	set_obj(&G(ks)->cfunction_tbl[nr], func);
	G(ks)->nr_builtin_cfunction++;
}

static void exit_cfunction_cache(ktap_state *ks)
{
	kp_free(ks, G(ks)->cfunction_tbl);
}

static int init_cfunction_cache(ktap_state *ks)
{
	G(ks)->cfunction_tbl = kp_zalloc(ks, sizeof(ktap_value) * 128);
	if (!G(ks)->cfunction_tbl)
		return -ENOMEM;

	return 0;
}

/* function for register library */
int kp_register_lib(ktap_state *ks, const char *libname, const ktap_Reg *funcs)
{
	const ktap_value *gt = kp_tab_getint(hvalue(&G(ks)->registry),
					     KTAP_RIDX_GLOBALS);
	ktap_tab *target_tbl;
	int i;

	/* lib is null when register baselib function */
	if (libname == NULL)
		target_tbl = hvalue(gt);
	else {
		ktap_value key, val;

		/* calculate the function number contained by this library */
		for (i = 0; funcs[i].name != NULL; i++) {
		}

		target_tbl = kp_tab_new(ks, 0, i + 1);
		if (!target_tbl)
			return -1;

		set_string(&key, kp_str_new(ks, libname));
		set_table(&val, target_tbl);
		kp_tab_setvalue(ks, hvalue(gt), &key, &val);
	}

	for (i = 0; funcs[i].name != NULL; i++) {
		ktap_value func_name, cl;

		set_string(&func_name, kp_str_new(ks, funcs[i].name));
		set_cfunction(&cl, funcs[i].func);
		kp_tab_setvalue(ks, target_tbl, &func_name, &cl);

		cfunction_cache_add(ks, &cl);
	}

	return 0;
}

static int init_registry(ktap_state *ks)
{
	ktap_tab *registry = kp_tab_new(ks, 2, 0);
	ktap_value global_tbl;
	ktap_value mt;
	ktap_tab *t;

	if (!registry)
		return -1;

	set_table(&G(ks)->registry, registry);
	set_thread(&mt, ks);
	kp_tab_setint(ks, registry, KTAP_RIDX_MAINTHREAD, &mt);

	/* assume there will have max 1024 global variables */
	t = kp_tab_new(ks, 0, 1024);
	if (!t)
		return -1;

	set_table(&global_tbl, t);
	kp_tab_setint(ks, registry, KTAP_RIDX_GLOBALS, &global_tbl);

	return 0;
}

static int init_arguments(ktap_state *ks, int argc, char __user **user_argv)
{
	const ktap_value *gt = kp_tab_getint(hvalue(&G(ks)->registry),
			   KTAP_RIDX_GLOBALS);
	ktap_tab *global_tbl = hvalue(gt);
	ktap_tab *arg_tbl = kp_tab_new(ks, argc, 1);
	ktap_value arg_tblval;
	ktap_value arg_tsval;
	char **argv;
	int i, ret;

	if (!arg_tbl)
		return -1;

	set_string(&arg_tsval, kp_str_new(ks, "arg"));
	set_table(&arg_tblval, arg_tbl);
	kp_tab_setvalue(ks, global_tbl, &arg_tsval, &arg_tblval);

	if (!argc)
		return 0;

	if (argc > 1024)
		return -EINVAL;

	argv = kzalloc(argc * sizeof(char *), GFP_KERNEL);
	if (!argv)
		return -ENOMEM;

	ret = copy_from_user(argv, user_argv, argc * sizeof(char *));
	if (ret < 0) {
		kfree(argv);
		return -EFAULT;
	}

	ret = 0;
	for (i = 0; i < argc; i++) {
		ktap_value val;
		char __user *ustr = argv[i];
		char *kstr;
		int len;
		int res;

		len = strlen_user(ustr);
		if (len > 0x1000) {
			ret = -EINVAL;
			break;
		}

		kstr = kmalloc(len + 1, GFP_KERNEL);
		if (!kstr) {
			ret = -ENOMEM;
			break;
		}

		if (strncpy_from_user(kstr, ustr, len) < 0) {
			ret = -EFAULT;
			break;
		}

		kstr[len] = '\0';

		if (!kstrtoint(kstr, 10, &res)) {
			set_number(&val, res);
		} else
			set_string(&val, kp_str_new(ks, kstr));

		kp_tab_setint(ks, arg_tbl, i, &val);

		kfree(kstr);
	}

	kfree(argv);
	return ret;
}

static void free_percpu_data(ktap_state *ks)
{
	int i, j;

	for (i = 0; i < KTAP_PERCPU_DATA_MAX; i++) {
		for (j = 0; j < PERF_NR_CONTEXTS; j++)
			free_percpu(G(ks)->pcpu_data[i][j]);
	}

	for (j = 0; j < PERF_NR_CONTEXTS; j++)
		if (G(ks)->recursion_context[j])
			free_percpu(G(ks)->recursion_context[j]);
}

static int init_percpu_data(ktap_state *ks)
{
	int data_size[KTAP_PERCPU_DATA_MAX] = {
		sizeof(ktap_state), KTAP_STACK_SIZE_BYTES,
		KTAP_PERCPU_BUFFER_SIZE, KTAP_PERCPU_BUFFER_SIZE,
		sizeof(ktap_btrace) + (KTAP_MAX_STACK_ENTRIES *
			sizeof(unsigned long))};
	int i, j;

	for (i = 0; i < KTAP_PERCPU_DATA_MAX; i++) {
		for (j = 0; j < PERF_NR_CONTEXTS; j++) {
			void __percpu *data = __alloc_percpu(data_size[i],
							     __alignof__(char));
			if (!data)
				goto fail;
			G(ks)->pcpu_data[i][j] = data;
		}
	}

	for (j = 0; j < PERF_NR_CONTEXTS; j++) {
		G(ks)->recursion_context[j] = alloc_percpu(int);
		if (!G(ks)->recursion_context[j])
			goto fail;
	}

	return 0;

 fail:
	free_percpu_data(ks);
	return -ENOMEM;
}

static void init_ktap_stack(ktap_state *ks)
{
	ktap_callinfo *ci;

	/* init all stack vaule to nil */
	memset(ks->stack, 0, KTAP_STACK_SIZE_BYTES);

	ks->top = ks->stack;
	ks->stack_last = ks->stack + KTAP_STACK_SIZE;

	ci = &ks->baseci;
	ci->callstatus = 0;
	ci->func = ks->top;
	ci->top = ks->top + KTAP_MIN_RESERVED_STACK_SIZE;
	ks->ci = ci;
}

static void free_all_ci(ktap_state *ks)
{
	int cpu, j;

	for_each_possible_cpu(cpu) {
		for (j = 0; j < PERF_NR_CONTEXTS; j++) {
			void *pcd = G(ks)->pcpu_data[KTAP_PERCPU_DATA_STATE][j];
			ktap_state *ks;

			if (!pcd)
				break;

			ks = per_cpu_ptr(pcd, cpu);
			if (!ks)
				break;

			free_ci(ks);
		}
	}

	free_ci(ks);
}

void kp_thread_exit(ktap_state *ks)
{
	/* free local allocation objects, like annotate strings */
	if (ks->gclist)
		kp_obj_free_gclist(ks, ks->gclist);
	if (ks->openupval)
		kp_obj_free_gclist(ks, ks->openupval);
}

ktap_state *kp_thread_new(ktap_state *mainthread)
{
	ktap_state *ks;

	ks = kp_percpu_data(mainthread, KTAP_PERCPU_DATA_STATE);
	ks->stack = kp_percpu_data(mainthread, KTAP_PERCPU_DATA_STACK);
	G(ks) = G(mainthread);
	ks->gclist = NULL;
	init_ktap_stack(ks);
	return ks;
}

/*
 * wait ktapio thread read all content in ring buffer.
 *
 * Here we use stupid approach to sync with ktapio thread,
 * note that we cannot use semaphore/completion/other sync method,
 * because ktapio thread could be killed by SIG_KILL in anytime, there
 * have no safe way to up semaphore or wake waitqueue before thread exit.
 *
 * we also cannot use waitqueue of current->signal->wait_chldexit to sync
 * exit, becasue mainthread and ktapio thread are in same thread group.
 *
 * Also ktap mainthread must wait ktapio thread exit, otherwise ktapio
 * thread will oops when access ktap structure.
 */
static void wait_user_completion(ktap_state *ks)
{
	struct task_struct *tsk = G(ks)->task;
	G(ks)->wait_user = 1;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		/* sleep for 100 msecs, and try again. */
		schedule_timeout(HZ / 10);

		if (get_nr_threads(tsk) == 1)
			break;
	}
}

static void sleep_loop(ktap_state *ks,
			int (*actor)(ktap_state *ks, void *arg), void *arg)
{
	while (!ks->stop) {
		set_current_state(TASK_INTERRUPTIBLE);
		/* sleep for 100 msecs, and try again. */
		schedule_timeout(HZ / 10);

		if (actor(ks, arg))
			return;
	}
}

static int sl_wait_task_pause_actor(ktap_state *ks, void *arg)
{
	struct task_struct *task = (struct task_struct *)arg;

	if (task->state)
		return 1;
	else
		return 0;
}

static int sl_wait_task_exit_actor(ktap_state *ks, void *arg)
{
	struct task_struct *task = (struct task_struct *)arg;

	if (signal_pending(current)) {
		flush_signals(current);

		/* newline for handle CTRL+C display as ^C */
		kp_puts(ks, "\n");
		return 1;
	}

	/* stop waiting if target pid is exited */
	if (task && task->state == TASK_DEAD)
			return 1;

	return 0;
}

/* kp_wait: used for mainthread waiting for exit */
static void kp_wait(ktap_state *ks)
{
	struct task_struct *task = G(ks)->trace_task;

	if (G(ks)->exit)
		return;

	ks->stop = 0;

	if (G(ks)->parm->workload) {
		/* make sure workload is in pause state
		 * so it won't miss the signal */
		sleep_loop(ks, sl_wait_task_pause_actor, task);
		/* tell workload process to start executing */
		send_sig(SIGINT, G(ks)->trace_task, 0);
	}

	if (!G(ks)->parm->quiet)
		kp_printf(ks, "Tracing... Hit Ctrl-C to end.\n");

	sleep_loop(ks, sl_wait_task_exit_actor, task);
}

static unsigned int kp_stub_exit_instr;

static inline void set_next_as_exit(ktap_state *ks)
{
	ktap_callinfo *ci;

	ci = ks->ci;
	if (!ci)
		return;

	ci->u.l.savedpc = &kp_stub_exit_instr;

	/* See precall, ci changed to ci->prev after invoke C function */
	if (ci->prev) {
		ci = ci->prev;
		ci->u.l.savedpc = &kp_stub_exit_instr;
	}
}

/*
 * This function only tell ktapvm this thread want to exit,
 * let mainthread handle real exit work later.
 */
void kp_prepare_to_exit(ktap_state *ks)
{
	set_next_as_exit(ks);

	G(ks)->mainthread->stop = 1;
	G(ks)->exit = 1;
}

void kp_init_exit_instruction(void)
{
	SET_OPCODE(kp_stub_exit_instr, OP_EXIT);
}

/*
 * Be careful in stats_cleanup, only can use kp_printf, since almost
 * all ktap resources already freed now.
 */
static void exit_stats(ktap_state *ks)
{
	ktap_stats __percpu *stats = G(ks)->stats;
	int mem_allocated = 0, nr_mem_allocate = 0;
	int events_hits = 0, events_missed = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		ktap_stats *per_stats = per_cpu_ptr(stats, cpu);
		mem_allocated += per_stats->mem_allocated;
		nr_mem_allocate += per_stats->nr_mem_allocate;
		events_hits += per_stats->events_hits;
		events_missed += per_stats->events_missed;
	}

	kp_verbose_printf(ks, "ktap stats:\n");
	kp_verbose_printf(ks, "memory allocated size: %d\n", mem_allocated);
	kp_verbose_printf(ks, "memory allocate num: %d\n", nr_mem_allocate);
	kp_verbose_printf(ks, "events_hits: %d\n", events_hits);
	kp_verbose_printf(ks, "events_missed: %d\n", events_missed);

	if (stats)
		free_percpu(stats);
}

static int init_stats(ktap_state *ks)
{
	ktap_stats __percpu *stats = alloc_percpu(ktap_stats);
	if (!stats)
		return -ENOMEM;

	G(ks)->stats = stats;
	return 0;
}

/*
 * ktap exit, free all resources.
 */
void kp_final_exit(ktap_state *ks)
{
	if (!list_empty(&G(ks)->probe_events_head) ||
	    !list_empty(&G(ks)->timers))
		kp_wait(ks);

	kp_exit_timers(ks);
	kp_probe_exit(ks);

	/* free all resources got by ktap */
#ifdef CONFIG_KTAP_FFI
	ffi_free_symbols(ks);
#endif
	kp_str_freeall(ks);
	kp_obj_freeall(ks);
	exit_cfunction_cache(ks);

	kp_thread_exit(ks);
	kp_free(ks, ks->stack);
	free_all_ci(ks);

	free_percpu_data(ks);
	free_cpumask_var(G(ks)->cpumask);

	exit_stats(ks);
	wait_user_completion(ks);

	/* should invoke after wait_user_completion */
	if (G(ks)->trace_task)
		put_task_struct(G(ks)->trace_task);

	kp_transport_exit(ks);
	kp_free(ks, ks);
}

/*
 * ktap mainthread initization
 */
ktap_state *kp_newstate(ktap_parm *parm, struct dentry *dir)
{
	ktap_state *ks;
	pid_t pid;
	int cpu;

	ks = kzalloc(sizeof(ktap_state) + sizeof(ktap_global_state),
		     GFP_KERNEL);
	if (!ks)
		return NULL;

	G(ks) = (ktap_global_state *)(ks + 1);
	G(ks)->mainthread = ks;
	G(ks)->seed = 201236; /* todo: make more random in future */
	G(ks)->task = current;
	G(ks)->parm = parm;
	G(ks)->str_lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;
	G(ks)->uvhead.u.l.prev = &G(ks)->uvhead;
	G(ks)->uvhead.u.l.next = &G(ks)->uvhead;
	G(ks)->exit = 0;
	INIT_LIST_HEAD(&(G(ks)->timers));
	INIT_LIST_HEAD(&(G(ks)->probe_events_head));

	if (init_stats(ks))
		goto out;
	if (kp_transport_init(ks, dir))
		goto out;

	ks->stack = kp_malloc(ks, KTAP_STACK_SIZE_BYTES);
	if (!ks->stack)
		goto out;

	init_ktap_stack(ks);

	pid = (pid_t)parm->trace_pid;
	if (pid != -1) {
		struct task_struct *task;

		rcu_read_lock();
		task = pid_task(find_vpid(pid), PIDTYPE_PID);
		if (!task) {
			kp_error(ks, "cannot find pid %d\n", pid);
			rcu_read_unlock();
			goto out;
		}
		G(ks)->trace_task = task;
		get_task_struct(task);
		rcu_read_unlock();
	}

	if( !alloc_cpumask_var(&G(ks)->cpumask, GFP_KERNEL))
		goto out;

	cpumask_copy(G(ks)->cpumask, cpu_online_mask);

	cpu = parm->trace_cpu;
	if (cpu != -1) {
		if (!cpu_online(cpu)) {
			kp_error(ks, "ktap: cpu %d is not online\n", cpu);
			goto out;
		}

		cpumask_clear(G(ks)->cpumask);
		cpumask_set_cpu(cpu, G(ks)->cpumask);
	}

	if (init_cfunction_cache(ks))
		goto out;

	kp_strtab_resize(ks, 512); /* set inital string hashtable size */

	if (init_registry(ks))
		goto out;
	if (init_arguments(ks, parm->argc, parm->argv))
		goto out;

	/* init library */
	if (kp_init_baselib(ks))
		goto out;
	if (kp_init_kdebuglib(ks))
		goto out;
	if (kp_init_timerlib(ks))
		goto out;
	if (kp_init_ansilib(ks))
		goto out;
#ifdef CONFIG_KTAP_FFI
	if (kp_init_ffilib(ks))
		goto out;
#endif
	if (kp_init_tablelib(ks))
		goto out;

	if (init_percpu_data(ks))
		goto out;
	if (kp_probe_init(ks))
		goto out;

	return ks;

 out:
	G(ks)->exit = 1;
	kp_final_exit(ks);
	return NULL;
}

