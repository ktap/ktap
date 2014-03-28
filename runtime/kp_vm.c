/*
 * kp_vm.c - ktap script virtual machine in Linux kernel
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Adapted from luajit and lua interpreter.
 * Copyright (C) 2005-2014 Mike Pall.
 * Copyright (C) 1994-2008 Lua.org, PUC-Rio.
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
#include "../include/ktap_bc.h"
#include "../include/ktap_ffi.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_mempool.h"
#include "kp_tab.h"
#include "kp_transport.h"
#include "kp_vm.h"
#include "kp_events.h"

#define KTAP_MIN_RESERVED_STACK_SIZE 20
#define KTAP_STACK_SIZE		120 /* enlarge this value for big stack */
#define KTAP_STACK_SIZE_BYTES	(KTAP_STACK_SIZE * sizeof(ktap_val_t))

#define KTAP_PERCPU_BUFFER_SIZE	(3 * PAGE_SIZE)

static ktap_cfunction gfunc_get(ktap_state_t *ks, int idx);
static int gfunc_getidx(ktap_global_state_t *g, ktap_cfunction cfunc);

static ktap_str_t *str_concat(ktap_state_t *ks, StkId top, int start, int end)
{
	int i, len = 0;
	ktap_str_t *ts;
	char *ptr, *buffer;

	for (i = start; i <= end; i++) {
		if (!is_string(top + i)) {
			kp_error(ks, "cannot concat non-string\n");
			return NULL;
		}

		len += rawtsvalue(top + i)->len;
	}

	if (len >= KTAP_PERCPU_BUFFER_SIZE) {
		kp_error(ks, "Error: too long string concatenation\n");
		return NULL;
	}

	preempt_disable_notrace();

	buffer = kp_this_cpu_print_buffer(ks);
	ptr = buffer;

	for (i = start; i <= end; i++) {
		int len = rawtsvalue(top + i)->len;
		strncpy(ptr, svalue(top + i), len);
		ptr += len;
	}
	ts = kp_str_new(ks, buffer, len);

	preempt_enable_notrace();

	return ts;
}

static ktap_upval_t *findupval(ktap_state_t *ks, StkId slot)
{
	ktap_global_state_t *g = G(ks);
	ktap_upval_t **pp = &ks->openupval;
	ktap_upval_t *p;
	ktap_upval_t *uv;

	while (*pp != NULL && (p = *pp)->v >= slot) {
		if (p->v == slot) {  /* found a corresponding upvalue? */
			return p;
		}
		pp = (ktap_upval_t **)&p->nextgc;
	}

	/* not found: create a new one */
	uv = (ktap_upval_t *)kp_malloc(ks, sizeof(ktap_upval_t));
	if (!uv)
		return NULL;
	uv->gct = ~KTAP_TUPVAL;
	uv->closed = 0; /* still open */
	uv->v = slot;  /* current value lives in the stack */
	/* Insert into sorted list of open upvalues. */
	uv->nextgc = (ktap_obj_t *)*pp;
	*pp = uv;
	uv->prev = &g->uvhead;  /* double link it in `uvhead' list */
	uv->next = g->uvhead.next;
	uv->next->prev = uv;
	g->uvhead.next = uv;
	return uv;
}

static void unlinkupval(ktap_upval_t *uv)
{
	uv->next->prev = uv->prev;  /* remove from `uvhead' list */
	uv->prev->next = uv->next;
}

void kp_freeupval(ktap_state_t *ks, ktap_upval_t *uv)
{
	if (!uv->closed)  /* is it open? */
		unlinkupval(uv);  /* remove from open list */
	kp_free(ks, uv);  /* free upvalue */
}

/* close upvals */
static void func_closeuv(ktap_state_t *ks, StkId level)
{
	ktap_upval_t *uv;
	ktap_global_state_t *g = G(ks);
	while (ks->openupval != NULL &&
		(uv = ks->openupval)->v >= level) {
		ktap_obj_t *o = obj2gco(uv);
		/* remove from `open' list */
		ks->openupval = (ktap_upval_t *)uv->nextgc;
		unlinkupval(uv);  /* remove upvalue from 'uvhead' list */
		set_obj(&uv->tv, uv->v);  /* move value to upvalue slot */
		uv->v = &uv->tv;  /* now current value lives here */
		uv->closed = 1;
		gch(o)->nextgc = g->allgc; /* link upvalue into 'allgc' list */
		g->allgc = o;
	}
}

#define SIZE_KTAP_FUNC(n) (sizeof(ktap_func_t) - sizeof(ktap_obj_t *) + \
			   sizeof(ktap_obj_t *) * (n))
static ktap_func_t *func_new_empty(ktap_state_t *ks, ktap_proto_t *pt)
{
	ktap_func_t *fn;

	/* only mainthread can create new function */
	if (ks != G(ks)->mainthread) {
		kp_error(ks, "only mainthread can create function\n");
		return NULL;
	}

	fn = (ktap_func_t *)kp_obj_new(ks, SIZE_KTAP_FUNC(pt->sizeuv));
	if (!fn)
		return NULL;
	fn->gct = ~KTAP_TFUNC;
	fn->nupvalues = 0; /* Set to zero until upvalues are initialized. */
	fn->pc = proto_bc(pt);
	fn->p = pt;

	return fn;
}

static ktap_func_t *func_new(ktap_state_t *ks, ktap_proto_t *pt,
			     ktap_func_t *parent, ktap_val_t *base)
{
	ktap_func_t *fn;
	int nuv = pt->sizeuv, i;

	fn = func_new_empty(ks, pt);
	if (!fn)
		return NULL;

	fn->nupvalues = nuv;
	for (i = 0; i < nuv; i++) {
		uint32_t v = proto_uv(pt)[i];
		ktap_upval_t *uv;

		if (v & PROTO_UV_LOCAL) {
			uv = findupval(ks, base + (v & 0xff));
			if (!uv)
				return NULL;
			uv->immutable = ((v /PROTO_UV_IMMUTABLE) & 1);
		} else {
			uv = parent->upvals[v];
		}
		fn->upvals[i] = uv;
	}
	return fn;
}

static inline int checkstack(ktap_state_t *ks, int n)
{
	if (unlikely(ks->stack_last - ks->top <= n)) {
		kp_error(ks, "stack overflow, please enlarge stack size\n");
		return -1;
	}
	return 0;
}

static StkId adjust_varargs(ktap_state_t *ks, ktap_proto_t *p, int actual)
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

static void poscall(ktap_state_t *ks, StkId func, StkId first_result,
		   int wanted)
{
	int i;

	for (i = wanted; i != 0 && first_result < ks->top; i--)
		set_obj(func++, first_result++);

	while(i-- > 0)
		set_nil(func++);
}

void kp_vm_call_proto(ktap_state_t *ks, ktap_proto_t *pt)
{
	ktap_func_t *fn;

	fn = func_new_empty(ks, pt);
	if (!fn)
		return;
	set_func(ks->top++, fn);
	kp_vm_call(ks, ks->top - 1, 0);
}

/*
 * Hot loop detaction
 *
 * Check hot loop detaction in three cases:
 * 1. jmp -x: this happens in 'while (expr) { ... }'
 * 2. FORPREP-FORLOOP
 * 3. TFORCALL-TFORLOOP
 */ 
static __always_inline int check_hot_loop(ktap_state_t *ks, int loop_count)
{
	if (unlikely(loop_count == kp_max_loop_count)) {
		kp_error(ks, "loop execute count exceed max limit(%d)\n",
			     kp_max_loop_count);
		return -1;
	}

	return 0;
}

#define dojump(i, e) { pc += (int)bc_d(i) - BCBIAS_J + e; }
#define donextjump  { instr = *pc; dojump(instr, 1); }

#define NUMADD(a, b)    ((a) + (b))
#define NUMSUB(a, b)    ((a) - (b))
#define NUMMUL(a, b)    ((a) * (b))
#define NUMDIV(a, b)    ((a) / (b))
#define NUMUNM(a)       (-(a))
#define NUMEQ(a, b)     ((a) == (b))
#define NUMLT(a, b)     ((a) < (b))
#define NUMLE(a, b)     ((a) <= (b))
#define NUMMOD(a, b)    ((a) % (b))

#define arith_VV(ks, op) { \
	ktap_val_t *rb = RB; \
	ktap_val_t *rc = RC; \
	if (is_number(rb) && is_number(rc)) { \
		ktap_number nb = nvalue(rb), nc = nvalue(rc); \
		set_number(RA, op(nb, nc)); \
	} else {	\
		kp_puts(ks, "Error: Cannot make arith operation\n");	\
		return;	\
	} }

#define arith_VN(ks, op) { \
	ktap_val_t *rb = RB; \
	if (is_number(rb)) { \
		ktap_number nb = nvalue(rb);\
		ktap_number nc = nvalue((ktap_val_t *)kbase + bc_c(instr));\
		set_number(RA, op(nb, nc)); \
	} else {	\
		kp_puts(ks, "Error: Cannot make arith operation\n");	\
		return;	\
	} }

#define arith_NV(ks, op) { \
	ktap_val_t *rb = RB; \
	if (is_number(rb)) { \
		ktap_number nb = nvalue(rb);\
		ktap_number nc = nvalue((ktap_val_t *)kbase + bc_c(instr));\
		set_number(RA, op(nc, nb)); \
	} else {	\
		kp_puts(ks, "Error: Cannot make arith operation\n");	\
		return;	\
	} }


static const char * const bc_names[] = {
#define BCNAME(name, ma, mb, mc, mt)       #name,
	BCDEF(BCNAME)
#undef BCNAME
	NULL
};


/*
 * ktap bytecode interpreter routine
 *
 *
 * kp_vm_call only can be used for:
 * 1). call ktap function, not light C function
 * 2). accept fixed argument function
 */
void kp_vm_call(ktap_state_t *ks, StkId func, int nresults)
{
	int loop_count = 0;
	ktap_func_t *fn;
	ktap_proto_t *pt;
	ktap_obj_t **kbase;
	unsigned int instr, op;
	const unsigned int *pc;
	StkId base; /* stack pointer */
	int multres = 0; /* temp varible */
	ktap_tab_t *gtab = G(ks)->gtab;

	/* use computed goto for opcode dispatch */

	static void *dispatch_table[] = {
#define BCNAME(name, ma, mb, mc, mt)       &&DO_BC_##name,
		BCDEF(BCNAME)
#undef BCNAME
	};

#define DISPATCH()				\
	do {					\
		instr = *(pc++);		\
		op = bc_op(instr);		\
		goto *dispatch_table[op];	\
	} while (0)

#define RA	(base + bc_a(instr))
#define RB	(base + bc_b(instr))
#define RC	(base + bc_c(instr))
#define RD	(base + bc_d(instr))
#define RKD	((ktap_val_t *)kbase + bc_d(instr))

	/*TODO: fix argument number mismatch, example: sort cmp closure */

	fn = clvalue(func);
	pt = fn->p;
	kbase = fn->p->k;
	base = func + 1;
	pc = proto_bc(pt) + 1;
	ks->top = base + pt->framesize;
	func->pcr = 0; /* no previous frame */

	/* main loop of interpreter */
	DISPATCH();

	while (1) {
	DO_BC_ISLT: /* Jump if A < D */
		if (!is_number(RA) || !is_number(RD)) {
			kp_error(ks, "compare with non-number\n");
			return;
		}

		if (nvalue(RA) >= nvalue(RD))
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISGE: /* Jump if A >= D */
		if (!is_number(RA) || !is_number(RD)) {
			kp_error(ks, "compare with non-number\n");
			return;
		}

		if (nvalue(RA) < nvalue(RD))
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISLE: /* Jump if A <= D */
		if (!is_number(RA) || !is_number(RD)) {
			kp_error(ks, "compare with non-number\n");
			return;
		}

		if (nvalue(RA) > nvalue(RD))
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISGT: /* Jump if A > D */
		if (!is_number(RA) || !is_number(RD)) {
			kp_error(ks, "compare with non-number\n");
			return;
		}

		if (nvalue(RA) <= nvalue(RD))
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISEQV: /* Jump if A = D */
		if (!kp_obj_equal(RA, RD))
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISNEV: /* Jump if A != D */
		if (kp_obj_equal(RA, RD))
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISEQS: { /* Jump if A = D */
		int idx = ~bc_d(instr);

		if (!is_string(RA) ||
				rawtsvalue(RA) != (ktap_str_t *)kbase[idx])
			pc++;
		else
			donextjump;
		DISPATCH();
		}
	DO_BC_ISNES: { /* Jump if A != D */
		int idx = ~bc_d(instr);

		if (is_string(RA) &&
			rawtsvalue(RA) == (ktap_str_t *)kbase[idx])
			pc++;
		else
			donextjump;
		DISPATCH();
		}
	DO_BC_ISEQN: /* Jump if A = D */
		if (!is_number(RA) || nvalue(RA) !=  nvalue(RKD))
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISNEN: /* Jump if A != D */
		if (is_number(RA) && nvalue(RA) ==  nvalue(RKD))
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISEQP: /* Jump if A = D */
		if (itype(RA) != ~bc_d(instr))
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISNEP: /* Jump if A != D */
		if (itype(RA) == ~bc_d(instr))
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISTC: /* Copy D to A and jump, if D is true */
		if (itype(RD) == KTAP_TNIL || itype(RD) == KTAP_TFALSE)
			pc++;
		else {
			set_obj(RA, RD);
			donextjump;
		}
		DISPATCH();
	DO_BC_ISFC: /* Copy D to A and jump, if D is false */
		if (itype(RD) != KTAP_TNIL && itype(RD) != KTAP_TFALSE)
			pc++;
		else {
			set_obj(RA, RD);
			donextjump;
		}
		DISPATCH();
	DO_BC_IST: /* Jump if D is true */
		if (itype(RD) == KTAP_TNIL || itype(RD) == KTAP_TFALSE)
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISF: /* Jump if D is false */
		/* only nil and false are considered false,
		 * all other values are true */
		if (itype(RD) != KTAP_TNIL && itype(RD) != KTAP_TFALSE)
			pc++;
		else
			donextjump;
		DISPATCH();
	DO_BC_ISTYPE: /* generated by genlibbc, not compiler; not used now */
	DO_BC_ISNUM:
		return;
	DO_BC_MOV: /* Copy D to A */
		set_obj(RA, RD);
		DISPATCH();
	DO_BC_NOT: /* Set A to boolean not of D */
		if (itype(RD) == KTAP_TNIL || itype(RD) == KTAP_TFALSE)
			setitype(RA, KTAP_TTRUE);
		else
			setitype(RA, KTAP_TFALSE);

		DISPATCH();
	DO_BC_UNM: /* Set A to -D (unary minus) */
		if (!is_number(RD)) {
			kp_error(ks, "use '-' operator on non-number\n");
			return;
		}

		set_number(RA, -nvalue(RD));
		DISPATCH();
	DO_BC_ADDVN: /* A = B + C */
		arith_VN(ks, NUMADD);
		DISPATCH();
	DO_BC_SUBVN: /* A = B - C */
		arith_VN(ks, NUMSUB);
		DISPATCH();
	DO_BC_MULVN: /* A = B * C */
		arith_VN(ks, NUMMUL);
		DISPATCH();
	DO_BC_DIVVN: /* A = B / C */
		/* divide 0 checking */
		if (!nvalue((ktap_val_t *)kbase + bc_c(instr))) {
			kp_error(ks, "divide 0 arith operation\n");
			return;
		}
		arith_VN(ks, NUMDIV);
		DISPATCH();
	DO_BC_MODVN: /* A = B % C */
		/* divide 0 checking */
		if (!nvalue((ktap_val_t *)kbase + bc_c(instr))) {
			kp_error(ks, "mod 0 arith operation\n");
			return;
		}
		arith_VN(ks, NUMMOD);
		DISPATCH();
	DO_BC_ADDNV: /* A = C + B */
		arith_NV(ks, NUMADD);
		DISPATCH();
	DO_BC_SUBNV: /* A = C - B */
		arith_NV(ks, NUMSUB);
		DISPATCH();
	DO_BC_MULNV: /* A = C * B */
		arith_NV(ks, NUMMUL);
		DISPATCH();
	DO_BC_DIVNV: /* A = C / B */
		/* divide 0 checking */
		if (!nvalue(RB)){
			kp_error(ks, "divide 0 arith operation\n");
			return;
		}
		arith_NV(ks, NUMDIV);
		DISPATCH();
	DO_BC_MODNV: /* A = C % B */
		/* divide 0 checking */
		if (!nvalue(RB)){
			kp_error(ks, "mod 0 arith operation\n");
			return;
		}
		arith_NV(ks, NUMMOD);
		DISPATCH();
	DO_BC_ADDVV: /* A = B + C */
		arith_VV(ks, NUMADD);
		DISPATCH();
	DO_BC_SUBVV: /* A = B - C */
		arith_VV(ks, NUMSUB);
		DISPATCH();
	DO_BC_MULVV: /* A = B * C */
		arith_VV(ks, NUMMUL);
		DISPATCH();
	DO_BC_DIVVV: /* A = B / C */
		arith_VV(ks, NUMDIV);
		DISPATCH();
	DO_BC_MODVV: /* A = B % C */
		arith_VV(ks, NUMMOD);
		DISPATCH();
	DO_BC_POW: /* A = B ^ C, rejected */
		return;
	DO_BC_CAT: { /* A = B .. ~ .. C */
		/* The CAT instruction concatenates all values in
		 * variable slots B to C inclusive. */
		ktap_str_t *ts = str_concat(ks, base, bc_b(instr),
					    bc_c(instr));
		if (!ts)
			return;
		
		set_string(RA, ts);
		DISPATCH();
		}
	DO_BC_KSTR: { /* Set A to string constant D */
		int idx = ~bc_d(instr);
		set_string(RA, (ktap_str_t *)kbase[idx]);
		DISPATCH();
		}
	DO_BC_KCDATA: /* not used now */
		DISPATCH();
	DO_BC_KSHORT: /* Set A to 16 bit signed integer D */
		set_number(RA, bc_d(instr));
		DISPATCH();
	DO_BC_KNUM: /* Set A to number constant D */
		set_number(RA, nvalue(RKD));
		DISPATCH();
	DO_BC_KPRI: /* Set A to primitive D */
		setitype(RA, ~bc_d(instr));
		DISPATCH();
	DO_BC_KNIL: { /* Set slots A to D to nil */
		int i;
		for (i = 0; i <= bc_d(instr) - bc_a(instr); i++) {
			set_nil(RA + i);
		}
		DISPATCH();
		}
	DO_BC_UGET: /* Set A to upvalue D */
		set_obj(RA, fn->upvals[bc_d(instr)]->v);
		DISPATCH();
	DO_BC_USETV: /* Set upvalue A to D */
		set_obj(fn->upvals[bc_a(instr)]->v, RD);
		DISPATCH();
	DO_BC_UINCV: { /* upvalus[A] += D */
		ktap_val_t *v = fn->upvals[bc_a(instr)]->v;
		if (unlikely(!is_number(RD) || !is_number(v))) {
			kp_error(ks, "use '+=' on non-number\n");
			return;
		}
		set_number(v, nvalue(v) + nvalue(RD));
		DISPATCH();
		}
	DO_BC_USETS: { /* Set upvalue A to string constant D */
		int idx = ~bc_d(instr);
		set_string(fn->upvals[bc_a(instr)]->v,
				(ktap_str_t *)kbase[idx]);
		DISPATCH();
		}
	DO_BC_USETN: /* Set upvalue A to number constant D */
		set_number(fn->upvals[bc_a(instr)]->v, nvalue(RKD));
		DISPATCH();
	DO_BC_UINCN: { /* upvalus[A] += D */
		ktap_val_t *v = fn->upvals[bc_a(instr)]->v;
		if (unlikely(!is_number(v))) {
			kp_error(ks, "use '+=' on non-number\n");
			return;
		}
		set_number(v, nvalue(v) + nvalue(RKD));
		DISPATCH();
		}
	DO_BC_USETP: /* Set upvalue A to primitive D */
		setitype(fn->upvals[bc_a(instr)]->v, ~bc_d(instr));
		DISPATCH();
	DO_BC_UCLO: /* Close upvalues for slots . rbase and jump to target D */
		if (ks->openupval != NULL)
			func_closeuv(ks, RA);
		dojump(instr, 0);
		DISPATCH();
	DO_BC_FNEW: {
		/* Create new closure from prototype D and store it in A */
		int idx = ~bc_d(instr);
		ktap_func_t *subfn = func_new(ks, (ktap_proto_t *)kbase[idx],
					      fn, base);
		if (unlikely(!subfn))
			return;
		set_func(RA, subfn);
		DISPATCH();
		}
	DO_BC_TNEW: { /* Set A to new table with size D */
		/* 
		 * preallocate default narr and nrec,
		 * op_b and op_c is not used
		 * This would allocate more memory for some static table.
		 */
		ktap_tab_t *t = kp_tab_new_ah(ks, 0, 0);
		if (unlikely(!t))
			return;
		set_table(RA, t);
		DISPATCH();
		}
	DO_BC_TDUP: { /* Set A to duplicated template table D */
		int idx = ~bc_d(instr);
		ktap_tab_t *t = kp_tab_dup(ks, (ktap_tab_t *)kbase[idx]);
		if (!t)
			return;
		set_table(RA, t);
		DISPATCH();
		}
	DO_BC_GGET: { /* A = _G[D] */
		int idx = ~bc_d(instr);
		kp_tab_getstr(gtab, (ktap_str_t *)kbase[idx], RA);
		DISPATCH();
		}
	DO_BC_GSET: /* _G[D] = A, rejected. */
	DO_BC_GINC: /* _G[D] += A, rejected. */
		return;
	DO_BC_TGETV: /* A = B[C] */
		if (unlikely(!is_table(RB))) {
			kp_error(ks, "get key from non-table\n");
			return;
		}

		kp_tab_get(ks, hvalue(RB), RC, RA);
		DISPATCH();
	DO_BC_TGETS: { /* A = B[C] */
		int idx = ~bc_c(instr);

		if (unlikely(!is_table(RB))) {
			kp_error(ks, "get key from non-table\n");
			return;
		}
		kp_tab_getstr(hvalue(RB), (ktap_str_t *)kbase[idx], RA);
		DISPATCH();
		}
	DO_BC_TGETB: { /* A = B[C] */
		/* 8 bit literal C operand as an unsigned integer
		 * index (0..255)) */
		uint8_t idx = bc_c(instr);

		if (unlikely(!is_table(RB))) {
			kp_error(ks, "set key to non-table\n");
			return;
		}
		kp_tab_getint(hvalue(RB), idx, RA);
		DISPATCH();
		}
	DO_BC_TGETR: /* generated by genlibbc, not compiler, not used */
		return;
	DO_BC_TSETV: /* B[C] = A */
		if (unlikely(!is_table(RB))) {
			kp_error(ks, "set key to non-table\n");
			return;
		}
		kp_tab_set(ks, hvalue(RB), RC, RA);
		DISPATCH();
	DO_BC_TINCV: /* B[C] += A */
		if (unlikely(!is_table(RB))) {
			kp_error(ks, "set key to non-table\n");
			return;
		}
		if (unlikely(!is_number(RA))) {
			kp_error(ks, "use '+=' on non-number\n");
			return;
		}
		kp_tab_incr(ks, hvalue(RB), RC, nvalue(RA));
		DISPATCH();
	DO_BC_TSETS: { /* B[C] = A */
		int idx = ~bc_c(instr);

		if (unlikely(!is_table(RB))) {
			kp_error(ks, "set key to non-table\n");
			return;
		}
		kp_tab_setstr(ks, hvalue(RB), (ktap_str_t *)kbase[idx], RA);
		DISPATCH();
		}
	DO_BC_TINCS: { /* B[C] += A */
		int idx = ~bc_c(instr);

		if (unlikely(!is_table(RB))) {
			kp_error(ks, "set key to non-table\n");
			return;
		}
		if (unlikely(!is_number(RA))) {
			kp_error(ks, "use '+=' on non-number\n");
			return;
		}
		kp_tab_incrstr(ks, hvalue(RB), (ktap_str_t *)kbase[idx],
				nvalue(RA));
		DISPATCH();
		}
	DO_BC_TSETB: { /* B[C] = A */
		/* 8 bit literal C operand as an unsigned integer
		 * index (0..255)) */
		uint8_t idx = bc_c(instr);

		if (unlikely(!is_table(RB))) {
			kp_error(ks, "set key to non-table\n");
			return;
		}
		kp_tab_setint(ks, hvalue(RB), idx, RA);
		DISPATCH();
		}
	DO_BC_TINCB: { /* B[C] = A */
		uint8_t idx = bc_c(instr);

		if (unlikely(!is_table(RB))) {
			kp_error(ks, "set key to non-table\n");
			return;
		}
		if (unlikely(!is_number(RA))) {
			kp_error(ks, "use '+=' on non-number\n");
			return;
		}
		kp_tab_incrint(ks, hvalue(RB), idx, nvalue(RA));
		DISPATCH();
		}
	DO_BC_TSETM: /* don't support */
		return;
	DO_BC_TSETR: /* generated by genlibbc, not compiler, not used */
		return;
	DO_BC_CALLM:
	DO_BC_CALL: { /* b: return_number + 1; c: argument + 1 */
		int c = bc_c(instr);
		int nresults = bc_b(instr) - 1;
		StkId oldtop = ks->top;
		StkId newfunc = RA;

		if (op == BC_CALL && c != 0)
			ks->top = RA + c;
		else if (op == BC_CALLM)
			ks->top = RA + c + multres;

		if (itype(newfunc) == KTAP_TCFUNC) { /* light C function */
			ktap_cfunction f = fvalue(newfunc);
			int n;

			if (unlikely(checkstack(ks,
					KTAP_MIN_RESERVED_STACK_SIZE)))
				return;

			ks->func = newfunc;
			n = (*f)(ks);
			if (unlikely(n < 0)) /* error occured */
				return;
			poscall(ks, newfunc, ks->top - n, nresults);

			ks->top = oldtop;
			multres = n + 1; /* set to multres */
			DISPATCH();
		} else if (itype(newfunc) == KTAP_TFUNC) { /* ktap function */
			int n;

			func = newfunc;
			pt = clvalue(func)->p;

			if (unlikely(checkstack(ks, pt->framesize)))
				return;

			/* get number of real arguments */
			n = (int)(ks->top - func) - 1;

			/* complete missing arguments */
			for (; n < pt->numparams; n++)
				set_nil(ks->top++);

			base = (!(pt->flags & PROTO_VARARG)) ? func + 1 :
						adjust_varargs(ks, pt, n);

			fn = clvalue(func);
			pt = fn->p;
			kbase = pt->k;
			func->pcr = pc - 1; /* save pc */
			ks->top = base + pt->framesize;
			pc = proto_bc(pt) + 1; /* starting point */
			DISPATCH();
		} else {
			kp_error(ks, "attempt to call nil function\n");
			return;
		}
		}
	DO_BC_CALLMT: /* don't support */
		return;
	DO_BC_CALLT: { /* Tailcall: return A(A+1, ..., A+D-1) */
		StkId nfunc = RA;

		if (itype(nfunc) == KTAP_TCFUNC) { /* light C function */
			kp_error(ks, "don't support callt for C function");
			return;
		} else if (itype(nfunc) == KTAP_TFUNC) { /* ktap function */
			int aux;

			/*
			 * tail call: put called frame (n) in place of
			 * caller one (o)
			 */
			StkId ofunc = func; /* caller function */
			/* last stack slot filled by 'precall' */
			StkId lim = nfunc + 1 + clvalue(nfunc)->p->numparams;

			fn = clvalue(nfunc);
			ofunc->val = nfunc->val;

			/* move new frame into old one */
			for (aux = 1; nfunc + aux < lim; aux++)
				set_obj(ofunc + aux, nfunc + aux);

			pt = fn->p;
			kbase = pt->k;
			ks->top = base + pt->framesize;
			pc = proto_bc(pt) + 1; /* starting point */
			DISPATCH();
		} else {
			kp_error(ks, "attempt to call nil function\n");
			return;
		}
		}
	DO_BC_ITERC: /* don't support it now */
		return;
	DO_BC_ITERN: /* Specialized ITERC, if iterator function A-3 is next()*/
		/* detect hot loop */
		if (unlikely(check_hot_loop(ks, loop_count++) < 0))
			return;

		if (kp_tab_next(ks, hvalue(RA - 2), RA)) {
			donextjump; /* Get jump target from ITERL */
		} else {
			pc++; /* jump to ITERL + 1 */
		}
		DISPATCH();
	DO_BC_VARG: /* don't support */
		return;
	DO_BC_ISNEXT: /* Verify ITERN specialization and jump */
		if (!is_cfunc(RA - 3) || !is_table(RA - 2) || !is_nil(RA - 1)
			|| fvalue(RA - 3) != (ktap_cfunction)kp_tab_next) {
			/* Despecialize bytecode if any of the checks fail. */
			setbc_op(pc - 1, BC_JMP);
			dojump(instr, 0);
			setbc_op(pc, BC_ITERC);
		} else {
			dojump(instr, 0);
			set_nil(RA); /* init control variable */
		}
		DISPATCH();
	DO_BC_RETM: /* don't support return multiple values */
	DO_BC_RET:
		return;
	DO_BC_RET0:
		/* if it's called from external invocation, just return */
		if (!func->pcr)
			return;

		pc = func->pcr; /* restore PC */

		multres = bc_d(instr);
		set_nil(func);

		base = func - bc_a(*pc);
		func = base - 1;
		fn = clvalue(func);
		kbase = fn->p->k;
		ks->top = base + pt->framesize;
		pc++;

		DISPATCH();
	DO_BC_RET1:
		/* if it's called from external invocation, just return */
		if (!func->pcr)
			return;

		pc = func->pcr; /* restore PC */

		multres = bc_d(instr);
		set_obj(base - 1, RA); /* move result */

		base = func - bc_a(*pc);
		func = base - 1;
		fn = clvalue(func);
		kbase = fn->p->k;
		ks->top = base + pt->framesize;
		pc++;

		DISPATCH();
	DO_BC_FORI: { /* Numeric 'for' loop init */
		ktap_number idx;
		ktap_number limit;
		ktap_number step;

		if (unlikely(!is_number(RA) || !is_number(RA + 1) ||
				!is_number(RA + 2))) {
			kp_error(ks, KTAP_QL("for")
				 " init/limit/step value must be a number\n");
			return;
		}

		idx = nvalue(RA);
		limit = nvalue(RA + 1);
		step = nvalue(RA + 2);

		if (NUMLT(0, step) ? NUMLE(idx, limit) : NUMLE(limit, idx)) {
			set_number(RA + 3, nvalue(RA));
		} else {
			dojump(instr, 0);
		}
		DISPATCH();
		}
	DO_BC_JFORI: /* not used */
		return;
	DO_BC_FORL: { /* Numeric 'for' loop */
		ktap_number step = nvalue(RA + 2);
		/* increment index */
		ktap_number idx = NUMADD(nvalue(RA), step);
		ktap_number limit = nvalue(RA + 1);
		if (NUMLT(0, step) ? NUMLE(idx, limit) : NUMLE(limit, idx)) {
			dojump(instr, 0); /* jump back */
			set_number(RA, idx);  /* update internal index... */
			set_number(RA + 3, idx);  /* ...and external index */
		}

		if (unlikely(check_hot_loop(ks, loop_count++) < 0))
			return;

		DISPATCH();
		}
	DO_BC_IFORL: /* not used */
	DO_BC_JFORL:
	DO_BC_ITERL:
	DO_BC_IITERL:
	DO_BC_JITERL:
		return;
	DO_BC_LOOP: /* Generic loop */
		/* ktap use this bc to detect hot loop */
		if (unlikely(check_hot_loop(ks, loop_count++) < 0))
			return;
		DISPATCH();
	DO_BC_ILOOP: /* not used */
	DO_BC_JLOOP:
		return;
	DO_BC_JMP: /* Jump */
		dojump(instr, 0);
		DISPATCH();
	DO_BC_FUNCF: /* function header, not used */
	DO_BC_IFUNCF:
	DO_BC_JFUNCF:
	DO_BC_FUNCV:
	DO_BC_IFUNCV:
	DO_BC_JFUNCV:
	DO_BC_FUNCC:
	DO_BC_FUNCCW:	
		return;
	DO_BC_VARGN: /* arg0 .. arg9*/
		if (unlikely(!ks->current_event)) {
			kp_error(ks, "invalid event context\n");
			return;
		}

		kp_event_getarg(ks, RA, bc_d(instr));
		DISPATCH();
	DO_BC_VARGSTR: { /* argstr */
		/*
		 * If you pass argstr to print/printf function directly,
		 * then no extra string generated, so don't worry string
		 * poll size for below case:
		 *     print(argstr)
		 *
		 * If you use argstr as table key like below, then it may
		 * overflow your string pool size, so be care of on it.
		 *     table[argstr] = V
		 *
		 * If you assign argstr to upval or table value like below,
		 * it don't really write string, just write type KTAP_TEVENTSTR,
		 * the value will be interpreted when value print out in valid
		 * event context, if context mismatch, error will report.
		 *     table[V] = argstr
		 *     upval = argstr
		 *
		 * If you want to save real string of argstr, then use it like
		 * below, again, be care of string pool size in this case.
		 *     table[V] = stringof(argstr)
		 *     upval = stringof(argstr)
		 */
		struct ktap_event_data *e = ks->current_event;

		if (unlikely(!e)) {
			kp_error(ks, "invalid event context\n");
			return;
		}

		if (e->argstr) /* argstr been stringified */
			set_string(RA, e->argstr);
		else
			set_eventstr(RA);
		DISPATCH();
		}
	DO_BC_VPROBENAME: { /* probename */
		struct ktap_event_data *e = ks->current_event;

		if (unlikely(!e)) {
			kp_error(ks, "invalid event context\n");
			return;
		}
		set_string(RA, e->event->name);
		DISPATCH();
		}
	DO_BC_VPID: /* pid */
		set_number(RA, (int)current->pid);
		DISPATCH();
	DO_BC_VTID: /* tid */
		set_number(RA, (int)task_pid_vnr(current));
		DISPATCH();
	DO_BC_VUID: { /* uid */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
		uid_t uid = from_kuid_munged(current_user_ns(), current_uid());
#else
		uid_t uid = current_uid();
#endif
		set_number(RA, (int)uid);
		DISPATCH();
		}
	DO_BC_VCPU: /* cpu */
		set_number(RA, smp_processor_id());
		DISPATCH();
	DO_BC_VEXECNAME: { /* execname */
		ktap_str_t *ts = kp_str_newz(ks, current->comm);
		if (unlikely(!ts))
			return;
		set_string(RA, ts);
		DISPATCH();
		}
	DO_BC_GFUNC: { /* Call built-in C function, patched by BC_GGET */
		ktap_cfunction cfunc = gfunc_get(ks, bc_d(instr));
		set_cfunc(RA, cfunc);
		DISPATCH();
		}
	}
}

/*
 * Validate byte code and static analysis.
 *
 * TODO: more type checking before real running.
 */
int kp_vm_validate_code(ktap_state_t *ks, ktap_proto_t *pt, ktap_val_t *base)
{
	const unsigned int *pc = proto_bc(pt) + 1;
	unsigned int instr, op;
	ktap_obj_t **kbase = pt->k;
	ktap_tab_t *gtab = G(ks)->gtab;
	int i;

#define RA	(base + bc_a(instr))
#define RB	(base + bc_b(instr))
#define RC	(base + bc_c(instr))
#define RD	(base + bc_d(instr))

	if (pt->framesize > KP_MAX_SLOTS) {
		kp_error(ks, "exceed max frame size %d\n", pt->framesize);
		return -1;
	}

	if (base + pt->framesize > ks->stack_last) {
		kp_error(ks, "stack overflow\n");
		return -1;
	}

	for (i = 0; i < pt->sizebc - 1; i++) {
		instr = *pc++;
		op = bc_op(instr);


		if (op >= BC__MAX) {
			kp_error(ks, "unknown byte code %d\n", op);
			return -1;
		}

		switch (op) {
		case BC_FNEW: {
			int idx = ~bc_d(instr);
			ktap_proto_t *newpt = (ktap_proto_t *)kbase[idx];
			if (kp_vm_validate_code(ks, newpt, RA + 1))
				return -1;

			break;
			}
		case BC_RETM: case BC_RET:
			kp_error(ks, "don't support return multiple values\n");
			return -1;
		case BC_GSET: case BC_GINC: { /* _G[D] = A, _G[D] += A */
			int idx = ~bc_d(instr);
			ktap_str_t *ts = (ktap_str_t *)kbase[idx];
			kp_error(ks, "cannot set global variable '%s'\n",
					getstr(ts));
			return -1;
			}
		case BC_GGET: {
			int idx = ~bc_d(instr);
			ktap_str_t *ts = (ktap_str_t *)kbase[idx];
			ktap_val_t val;
			kp_tab_getstr(gtab, ts, &val);
			if (is_nil(&val)) {
				kp_error(ks, "undefined global variable"
						" '%s'\n", getstr(ts));
				return -1;
			} else if (is_cfunc(&val)) {
				int idx = gfunc_getidx(G(ks), fvalue(&val));
				if (idx >= 0) {
					/* patch BC_GGET bytecode to BC_GFUNC */
					setbc_op(pc - 1, BC_GFUNC);
					setbc_d(pc - 1, idx);
				}
			}
			break;
			}
		case BC_ITERC:
			kp_error(ks, "ktap only support pairs iteraor\n");
			return -1;
		case BC_POW:
			kp_error(ks, "ktap don't support pow arith\n");
			return -1;
		}
	}

	return 0;
}

/* return cfunction by idx */
static ktap_cfunction gfunc_get(ktap_state_t *ks, int idx)
{
	return G(ks)->gfunc_tbl[idx];
}

/* get cfunction index, the index is for fast get cfunction in runtime */
static int gfunc_getidx(ktap_global_state_t *g, ktap_cfunction cfunc)
{
	int nr = g->nr_builtin_cfunction;
	ktap_cfunction *gfunc_tbl = g->gfunc_tbl;
	int i;

	for (i = 0; i < nr; i++) {
		if (gfunc_tbl[i] == cfunc)
			return i;
	}

	return -1;
}

static void gfunc_add(ktap_state_t *ks, ktap_cfunction cfunc)
{
	int nr = G(ks)->nr_builtin_cfunction;

	if (nr == KP_MAX_CACHED_CFUNCTION) {
		kp_error(ks, "please enlarge KP_MAX_CACHED_CFUNCTION %d\n",
				KP_MAX_CACHED_CFUNCTION);
		return;
	}
	G(ks)->gfunc_tbl[nr] = cfunc;
	G(ks)->nr_builtin_cfunction++;
}

/* function for register library */
int kp_vm_register_lib(ktap_state_t *ks, const char *libname,
		       const ktap_libfunc_t *funcs)
{
	ktap_tab_t *gtab = G(ks)->gtab;
	ktap_tab_t *target_tbl;
	int i;

	/* lib is null when register baselib function */
	if (libname == NULL)
		target_tbl = gtab;
	else {
		ktap_val_t key, val;
		ktap_str_t *ts = kp_str_newz(ks, libname);
		if (!ts)
			return -ENOMEM;

		/* calculate the function number contained by this library */
		for (i = 0; funcs[i].name != NULL; i++) {
		}

		target_tbl = kp_tab_new_ah(ks, 0, i + 1);
		if (!target_tbl)
			return -ENOMEM;

		set_string(&key, ts);
		set_table(&val, target_tbl);
		kp_tab_set(ks, gtab, &key, &val);
	}

	/* TODO: be care of same function name issue, foo() and tbl.foo() */
	for (i = 0; funcs[i].name != NULL; i++) {
		ktap_str_t *func_name = kp_str_newz(ks, funcs[i].name);
		ktap_val_t fn;

		if (unlikely(!func_name))
			return -ENOMEM;

		set_cfunc(&fn, funcs[i].func);
		kp_tab_setstr(ks, target_tbl, func_name, &fn);

		gfunc_add(ks, funcs[i].func);
	}

	return 0;
}

static int init_registry(ktap_state_t *ks)
{
	ktap_tab_t *registry = kp_tab_new_ah(ks, 2, 0);
	ktap_val_t gtbl;
	ktap_tab_t *t;

	if (!registry)
		return -1;

	set_table(&G(ks)->registry, registry);

	/* assume there will have max 1024 global variables */
	t = kp_tab_new_ah(ks, 0, 1024);
	if (!t)
		return -1;

	set_table(&gtbl, t);
	kp_tab_setint(ks, registry, KTAP_RIDX_GLOBALS, &gtbl);
	G(ks)->gtab = t;

	return 0;
}

static int init_arguments(ktap_state_t *ks, int argc, char __user **user_argv)
{
	ktap_tab_t *gtbl = G(ks)->gtab;
	ktap_tab_t *arg_tbl = kp_tab_new_ah(ks, argc, 1);
	ktap_val_t arg_tblval;
	ktap_val_t arg_tsval;
	ktap_str_t *argts = kp_str_newz(ks, "arg");
	char **argv;
	int i, ret;

	if (!arg_tbl)
		return -1;

	if (unlikely(!argts))
		return -ENOMEM;

	set_string(&arg_tsval, argts);
	set_table(&arg_tblval, arg_tbl);
	kp_tab_set(ks, gtbl, &arg_tsval, &arg_tblval);

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
		ktap_val_t val;
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
			kfree(kstr);
			ret = -EFAULT;
			break;
		}

		kstr[len] = '\0';

		if (!kstrtoint(kstr, 10, &res)) {
			set_number(&val, res);
		} else {
			ktap_str_t *ts = kp_str_newz(ks, kstr);
			if (unlikely(!ts)) {
				kfree(kstr);
				ret = -ENOMEM;
				break;
			}
				
			set_string(&val, ts);
		}

		kp_tab_setint(ks, arg_tbl, i, &val);

		kfree(kstr);
	}

	kfree(argv);
	return ret;
}

static void free_preserved_data(ktap_state_t *ks)
{
	int cpu, i, j;

	/* free stack for each allocated ktap_state */
	for_each_possible_cpu(cpu) {
		for (j = 0; j < PERF_NR_CONTEXTS; j++) {
			void *percpu_state = G(ks)->percpu_state[j];
			ktap_state_t *pks;

			if (!percpu_state)
				break;
			pks = per_cpu_ptr(percpu_state, cpu);
			if (!ks)
				break;
			kfree(pks->stack);
		}
	}

	/* free percpu ktap_state */
	for (i = 0; i < PERF_NR_CONTEXTS; i++) {
		if (G(ks)->percpu_state[i])
			free_percpu(G(ks)->percpu_state[i]);
	}

	/* free percpu ktap print buffer */
	for (i = 0; i < PERF_NR_CONTEXTS; i++) {
		if (G(ks)->percpu_print_buffer[i])
			free_percpu(G(ks)->percpu_print_buffer[i]);
	}

	/* free percpu ktap temp buffer */
	for (i = 0; i < PERF_NR_CONTEXTS; i++) {
		if (G(ks)->percpu_temp_buffer[i])
			free_percpu(G(ks)->percpu_temp_buffer[i]);
	}

	/* free percpu ktap recursion context flag */
	for (i = 0; i < PERF_NR_CONTEXTS; i++)
		if (G(ks)->recursion_context[i])
			free_percpu(G(ks)->recursion_context[i]);
}

#define ALLOC_PERCPU(size)  __alloc_percpu(size, __alignof__(char))
static int init_preserved_data(ktap_state_t *ks)
{
	void __percpu *data;
	int cpu, i, j;

	/* init percpu ktap_state */
	for (i = 0; i < PERF_NR_CONTEXTS; i++) {
		data = ALLOC_PERCPU(sizeof(ktap_state_t));
		if (!data)
			goto fail;
		G(ks)->percpu_state[i] = data;
	}

	/* init stack for each allocated ktap_state */
	for_each_possible_cpu(cpu) {
		for (j = 0; j < PERF_NR_CONTEXTS; j++) {
			void *percpu_state = G(ks)->percpu_state[j];
			ktap_state_t *pks;

			if (!percpu_state)
				break;
			pks = per_cpu_ptr(percpu_state, cpu);
			if (!ks)
				break;
			pks->stack = kzalloc(KTAP_STACK_SIZE_BYTES, GFP_KERNEL);
			if (!pks->stack)
				goto fail;

			pks->stack_last = pks->stack + KTAP_STACK_SIZE;
			G(pks) = G(ks);
		}
	}

	/* init percpu ktap print buffer */
	for (i = 0; i < PERF_NR_CONTEXTS; i++) {
		data = ALLOC_PERCPU(KTAP_PERCPU_BUFFER_SIZE);
		if (!data)
			goto fail;
		G(ks)->percpu_print_buffer[i] = data;
	}

	/* init percpu ktap temp buffer */
	for (i = 0; i < PERF_NR_CONTEXTS; i++) {
		data = ALLOC_PERCPU(KTAP_PERCPU_BUFFER_SIZE);
		if (!data)
			goto fail;
		G(ks)->percpu_temp_buffer[i] = data;
	}

	/* init percpu ktap recursion context flag */
	for (i = 0; i < PERF_NR_CONTEXTS; i++) {
		data = alloc_percpu(int);
		if (!data)
			goto fail;
		G(ks)->recursion_context[i] = data;
	}

	return 0;

 fail:
	free_preserved_data(ks);
	return -ENOMEM;
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
static void wait_user_completion(ktap_state_t *ks)
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

static void sleep_loop(ktap_state_t *ks,
			int (*actor)(ktap_state_t *ks, void *arg), void *arg)
{
	while (!ks->stop) {
		set_current_state(TASK_INTERRUPTIBLE);
		/* sleep for 100 msecs, and try again. */
		schedule_timeout(HZ / 10);

		if (actor(ks, arg))
			return;
	}
}

static int sl_wait_task_pause_actor(ktap_state_t *ks, void *arg)
{
	struct task_struct *task = (struct task_struct *)arg;

	if (task->state)
		return 1;
	else
		return 0;
}

static int sl_wait_task_exit_actor(ktap_state_t *ks, void *arg)
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

/* wait user interrupt, signal killed */
static void wait_user_interrupt(ktap_state_t *ks)
{
	struct task_struct *task = G(ks)->trace_task;

	if (G(ks)->state == KTAP_EXIT || G(ks)->state == KTAP_ERROR)
		return;

	/* let tracing goes now. */
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

/*
 * ktap exit, free all resources.
 */
void kp_vm_exit(ktap_state_t *ks)
{
	if (!list_empty(&G(ks)->events_head) ||
	    !list_empty(&G(ks)->timers))
		wait_user_interrupt(ks);

	kp_exit_timers(ks);
	kp_events_exit(ks);

	/* free all resources got by ktap */
#ifdef CONFIG_KTAP_FFI
	ffi_free_symbols(ks);
#endif
	kp_str_freeall(ks);
	kp_mempool_destroy(ks);

	func_closeuv(ks, 0); /* close all open upvals, let below call free it */
	kp_obj_freeall(ks);

	kp_vm_exit_thread(ks);
	kp_free(ks, ks->stack);

	free_preserved_data(ks);
	free_cpumask_var(G(ks)->cpumask);

	wait_user_completion(ks);

	/* should invoke after wait_user_completion */
	if (G(ks)->trace_task)
		put_task_struct(G(ks)->trace_task);

	kp_transport_exit(ks);
	kp_free(ks, ks); /* free self */
}

/*
 * ktap mainthread initization
 */
ktap_state_t *kp_vm_new_state(ktap_option_t *parm, struct dentry *dir)
{
	ktap_state_t *ks;
	ktap_global_state_t *g;
	pid_t pid;
	int cpu;

	ks = kzalloc(sizeof(ktap_state_t) + sizeof(ktap_global_state_t),
		     GFP_KERNEL);
	if (!ks)
		return NULL;

	G(ks) = (ktap_global_state_t *)(ks + 1);
	g = G(ks);
	g->mainthread = ks;
	g->task = current;
	g->parm = parm;
	g->str_lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;
	g->strmask = ~(int)0;
	g->uvhead.prev = &g->uvhead;
	g->uvhead.next = &g->uvhead;
	g->state = KTAP_RUNNING;
	INIT_LIST_HEAD(&(g->timers));
	INIT_LIST_HEAD(&(g->events_head));

	if (kp_transport_init(ks, dir))
		goto out;

	ks->stack = kp_malloc(ks, KTAP_STACK_SIZE_BYTES);
	if (!ks->stack)
		goto out;

	ks->stack_last = ks->stack + KTAP_STACK_SIZE;
	ks->top = ks->stack;

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
		g->trace_task = task;
		get_task_struct(task);
		rcu_read_unlock();
	}

	if( !alloc_cpumask_var(&g->cpumask, GFP_KERNEL))
		goto out;

	cpumask_copy(g->cpumask, cpu_online_mask);

	cpu = parm->trace_cpu;
	if (cpu != -1) {
		if (!cpu_online(cpu)) {
			kp_error(ks, "ktap: cpu %d is not online\n", cpu);
			goto out;
		}

		cpumask_clear(g->cpumask);
		cpumask_set_cpu(cpu, g->cpumask);
	}

	if (kp_mempool_init(ks, KP_MAX_MEMPOOL_SIZE))
		goto out;

	if (kp_str_resize(ks, 1024 - 1)) /* set string hashtable size */
		goto out;

	if (init_registry(ks))
		goto out;
	if (init_arguments(ks, parm->argc, parm->argv))
		goto out;

	/* init librarys */
	if (kp_lib_init_base(ks))
		goto out;
	if (kp_lib_init_kdebug(ks))
		goto out;
	if (kp_lib_init_timer(ks))
		goto out;
	if (kp_lib_init_ansi(ks))
		goto out;
#ifdef CONFIG_KTAP_FFI
	if (kp_lib_init_ffi(ks))
		goto out;
#endif
	if (kp_lib_init_table(ks))
		goto out;

	if (kp_lib_init_net(ks))
		goto out;

	if (init_preserved_data(ks))
		goto out;

	if (kp_events_init(ks))
		goto out;

	return ks;

 out:
	g->state = KTAP_ERROR;
	kp_vm_exit(ks);
	return NULL;
}

