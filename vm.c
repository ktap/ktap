/*
 * vm.c - ktap script virtual machine in Linux kernel
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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/hardirq.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include "ktap.h"

typedef int ptrdiff_t;
#define KTAP_MINSTACK 20

/* todo: enlarge maxstack for big system like 64-bit */
#define KTAP_MAXSTACK           15000

#define CIST_KTAP	(1 << 0) /* call is running a ktap function */
#define CIST_REENTRY	(1 << 2)

#define isktapfunc(ci)	((ci)->callstatus & CIST_KTAP)

void ktap_call(ktap_State *ks, StkId func, int nresults);

/* todo: compare l == r if both is tstring type? */
static int lessthan(ktap_State *ks, const Tvalue *l, const Tvalue *r)
{
	if (ttisnumber(l) && ttisnumber(r))
		return NUMLT(nvalue(l), nvalue(r));
	else if (ttisstring(l) && ttisstring(r))
		return tstring_cmp(rawtsvalue(l), rawtsvalue(r)) < 0;

	return 0;
}

static int lessequal(ktap_State *ks, const Tvalue *l, const Tvalue *r)
{
	if (ttisnumber(l) && ttisnumber(r))
		return NUMLE(nvalue(l), nvalue(r));
	else if (ttisstring(l) && ttisstring(r))
		return tstring_cmp(rawtsvalue(l), rawtsvalue(r)) <= 0;

	return 0;
}

static int fb2int (int x)
{
	int e = (x >> 3) & 0x1f;
	if (e == 0)
		return x;
	else
		return ((x & 7) + 8) << (e - 1);
}

static const Tvalue *ktap_tonumber(const Tvalue *obj, Tvalue *n)
{
	if (ttisnumber(obj))
		return obj;

	/* todo */
#if 0
	if (ttisstring(obj) &&
	    ktap_str2d(svalue(obj), tsvalue(obj)->len, &num)) {
		setnvalue(n, num);
		return n;
	} else
		return NULL;
#endif
	return NULL;
}

static Upval *findupval(ktap_State *ks, StkId level)
{
	global_State *g = G(ks);
	Gcobject **pp = &ks->openupval;
	Upval *p;
	Upval *uv;

	while (*pp != NULL && (p = gco2uv(*pp))->v >= level) {
		//Gcobject *o = obj2gco(p);
		if (p->v == level) {  /* found a corresponding upvalue? */
			//if (isdead(g, o))  /* is it dead? */
			//  changewhite(o);  /* resurrect it */
			return p;
		}
		//resetoldbit(o);  /* may create a newer upval after this one */
		pp = &p->next;
	}

	/* not found: create a new one */
	uv = &newobject(ks, KTAP_TUPVAL, sizeof(Upval), pp)->uv;
	uv->v = level;  /* current value lives in the stack */
	uv->u.l.prev = &g->uvhead;  /* double link it in `uvhead' list */
	uv->u.l.next = g->uvhead.u.l.next;
	uv->u.l.next->u.l.prev = uv;
	g->uvhead.u.l.next = uv;
	return uv;
}

/* todo: implement this*/
static void function_close (ktap_State *ks, StkId level)
{
}

/* create a new closure */
static void pushclosure(ktap_State *ks, Proto *p, Upval **encup, StkId base,
			StkId ra)
{
	int nup = p->sizeupvalues;
	Upvaldesc *uv = p->upvalues;
	int i;
	Closure *ncl = ktap_newlclosure(ks, nup);

	ncl->l.p = p;
	setcllvalue(ra, ncl);  /* anchor new closure in stack */

	/* fill in its upvalues */
	for (i = 0; i < nup; i++) {
		if (uv[i].instack) {
			/* upvalue refers to local variable? */
			ncl->l.upvals[i] = findupval(ks, base + uv[i].idx);
		} else {
			/* get upvalue from enclosing function */
			ncl->l.upvals[i] = encup[uv[i].idx];
		}
	}
	//p->cache = ncl;  /* save it on cache for reuse */
}

static void gettable(ktap_State *ks, const Tvalue *t, Tvalue *key, StkId val)
{
	setobj(ks, val, table_get(hvalue(t), key));
}

static void settable(ktap_State *ks, const Tvalue *t, Tvalue *key, StkId val)
{
	table_setvalue(ks, hvalue(t), key, val);
}



void growstack(ktap_State *ks, int n)
{
	Tvalue *oldstack;
	int lim;
	Callinfo *ci;
	Gcobject *up;
	int size = ks->stacksize;
	int needed = (int)(ks->top - ks->stack) + n;
	int newsize = 2 * size;

	if (newsize > KTAP_MAXSTACK)
		newsize = KTAP_MAXSTACK;

	if (newsize < needed)
		newsize = needed;

	if (newsize > KTAP_MAXSTACK) {  /* stack overflow? */
		ktap_runerror(ks, "stack overflow");
		return;
	}

	/* realloc stack */
	oldstack = ks->stack;
	lim = ks->stacksize;
	ktap_realloc(ks, ks->stack, ks->stacksize, newsize, Tvalue);

	for (; lim < newsize; lim++)
		setnilvalue(ks->stack + lim);
	ks->stacksize = newsize;
	ks->stack_last = ks->stack + newsize;

	/* correct stack */
	ks->top = (ks->top - oldstack) + ks->stack;
	for (up = ks->openupval; up != NULL; up = up->gch.next)
		gco2uv(up)->v = (gco2uv(up)->v - oldstack) + ks->stack;

	for (ci = ks->ci; ci != NULL; ci = ci->prev) {
		ci->top = (ci->top - oldstack) + ks->stack;
		ci->func = (ci->func - oldstack) + ks->stack;
		if (isktapfunc(ci))
			ci->u.l.base = (ci->u.l.base - oldstack) + ks->stack;
	}
	
}

static inline void checkstack(ktap_State *ks, int n)
{
	if (ks->stack_last - ks->top <= n)
		growstack(ks, n);
}


static StkId adjust_varargs(ktap_State *ks, Proto *p, int actual)
{
	int i;
	int nfixargs = p->numparams;
	StkId base, fixed;

	/* move fixed parameters to final position */
	fixed = ks->top - actual;  /* first fixed argument */
	base = ks->top;  /* final position of first argument */

	for (i=0; i < nfixargs; i++) {
		setobj(ks, ks->top++, fixed + i);
		setnilvalue(fixed + i);
	}

	return base;
}

static int poscall(ktap_State *ks, StkId first_result)
{
	Callinfo *ci;
	StkId res;
	int wanted, i;

	ci = ks->ci;

	res = ci->func;
	wanted = ci->nresults;

	ks->ci = ci = ci->prev;

	for (i = wanted; i != 0 && first_result < ks->top; i--)
		setobj(ks, res++, first_result++);

	while(i-- > 0)
		setnilvalue(res++);

	ks->top = res;

	return (wanted - (-1));
}

static Callinfo *extend_ci(ktap_State *ks)
{
	Callinfo *ci;

	ci = ktap_malloc(ks, sizeof(Callinfo));
	if (!ci) {
		printk("cannot alloc callinfo %d\n", sizeof(Callinfo));	
		dump_stack();
		return;
	}
	ks->ci->next = ci;
	ci->prev = ks->ci;
	ci->next = NULL;

	return ci;
}

static void free_ci(ktap_State *ks)
{
	Callinfo *ci = ks->ci;
	Callinfo *next = ci->next;
	ci->next = NULL;
	while ((ci = next) != NULL) {
		next = ci->next;
		ktap_free(ks, ci);
	}
}


#define next_ci(ks) (ks->ci = ks->ci->next ? ks->ci->next : extend_ci(ks))
#define savestack(ks, p)	((char *)(p) - (char *)ks->stack)
#define restorestack(ks, n)	((Tvalue *)((char *)ks->stack + (n)))

static int precall(ktap_State *ks, StkId func, int nresults)
{
	ktap_cfunction f;
	Callinfo *ci;
	Proto *p;
	StkId base;
	ptrdiff_t funcr = savestack(ks, func);
	int n;

	switch (ttype(func)) {
	case KTAP_TLCF: /* light C function */
		f = fvalue(func);
		goto CFUNC;
	case KTAP_TCCL: /* C closure */
		f = clcvalue(func)->f;
 CFUNC:
		checkstack(ks, KTAP_MINSTACK);
		ci = next_ci(ks);
		ci->nresults = nresults;
		ci->func = restorestack(ks, funcr);
		ci->top = ks->top + KTAP_MINSTACK;
		ci->callstatus = 0;
		n = (*f)(ks);
		poscall(ks, ks->top - n);
		return 1;
	case KTAP_TLCL:	
		p = CLVALUE(func)->p;
		checkstack(ks, p->maxstacksize);
		func = restorestack(ks, funcr);
		n = (int)(ks->top - func) - 1; /* number of real arguments */

		/* complete missing arguments */
		for (; n < p->numparams; n++)
			setnilvalue(ks->top++);

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

#define arith_op(op) { \
	Tvalue *rb = RKB(instr); \
	Tvalue *rc = RKC(instr); \
	if (ttisnumber(rb) && ttisnumber(rc)) { \
		ktap_Number nb = nvalue(rb), nc = nvalue(rc); \
		setnvalue(ra, op(nb, nc)); \
	} }

static void ktap_execute(ktap_State *ks)
{
	Callinfo *ci;
	LClosure *cl;
	Tvalue *k;
	unsigned int instr, opcode;
	StkId base; /* stack pointer */
	StkId ra; /* register pointer */
	int res, nresults; /* temp varible */

	ci = ks->ci;

 newframe:
	cl = CLVALUE(ci->func);
	k = cl->p->k;
	base = ci->u.l.base;

 mainloop:
	/* main loop of interpreter */

	instr = *(ci->u.l.savedpc++);
	opcode = GET_OPCODE(instr);

	/* ra is target register */
	ra = RA(instr);

//	ktap_printf(ks, "execute instruction: %s\n", ktap_opnames[opcode]);

	switch (opcode) {
	case OP_MOVE:
		setobj(ks, ra, base + GETARG_B(instr));
		break;
	case OP_LOADK:
		setobj(ks, ra, k + GETARG_Bx(instr));
		break;
	case OP_LOADKX:
		setobj(ks, ra, k + GETARG_Ax(*ci->u.l.savedpc++));
		break;
	case OP_LOADBOOL:
		setbvalue(ra, GETARG_B(instr));
		if (GETARG_C(instr))
			ci->u.l.savedpc++;
		break;
	case OP_LOADNIL: {
		int b = GETARG_B(instr);
		do {
			setnilvalue(ra++);
		} while (b--);
		break;
		}
	case OP_GETUPVAL: {
		int b = GETARG_B(instr);
		setobj(ks, ra, cl->upvals[b]->v);
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
	case OP_SETUPVAL: {
		Upval *uv = cl->upvals[GETARG_B(instr)];
		setobj(ks, uv->v, ra);
		break;
		}
	case OP_SETTABLE:
		settable(ks, ra, RKB(instr), RKC(instr));
		base = ci->u.l.base;
		break;
	case OP_NEWTABLE: {
		int b = GETARG_B(instr);
		int c = GETARG_C(instr);
		Table *t = table_new(ks);
		sethvalue(ra, t);
		if (b != 0 || c != 0)
			table_resize(ks, t, fb2int(b), fb2int(c));
		//checkGC(ks, ra + 1);
		break;
		}
	case OP_SELF: {
		StkId rb = RB(instr);
		setobj(ks, ra+1, rb);
		gettable(ks, rb, RKC(instr), ra);
		base = ci->u.l.base;
		break;
		}
	case OP_ADD:
		arith_op(NUMADD);
		break;
	case OP_SUB:
		arith_op(NUMSUB);
		break;
	case OP_MUL:
		arith_op(NUMMUL);
		break;
	case OP_DIV:
		arith_op(NUMDIV);
		break;
	case OP_MOD:
		/* todo: floor and pow in kernel */
		//arith_op(NUMMOD);
		break;
	case OP_POW:
		//arith_op(NUMPOW);
		break;
	case OP_UNM: {
		Tvalue *rb = RB(instr);
		if (ttisnumber(rb)) {
			ktap_Number nb = nvalue(rb);
			setnvalue(ra, NUMUNM(nb));
		}
		break;
		}
	case OP_NOT:
		res = isfalse(RB(instr));
		setbvalue(ra, res);
		break;
	case OP_LEN:
		objlen(ks, ra, RB(instr));
		break;
	case OP_CONCAT:
	case OP_JMP:
		dojump(ci, instr, 0);
		break;
	case OP_EQ: {
		Tvalue *rb = RKB(instr);
		Tvalue *rc = RKC(instr);
		if ((int)equalobj(ks, rb, rc) != GETARG_A(instr))
			ci->u.l.savedpc++;
		else
			donextjump(ci);

		base = ci->u.l.base;
		break;
		}
	case OP_LT:
		if (lessthan(ks, RKB(instr), RKC(instr)) != GETARG_A(instr))
			ci->u.l.savedpc++;
		else
			donextjump(ci);
		base = ci->u.l.base;
		break;
	case OP_LE:
		if (lessequal(ks, RKB(instr), RKC(instr)) != GETARG_A(instr))
			ci->u.l.savedpc++;
		else
			donextjump(ci);
		base = ci->u.l.base;
		break;

	case OP_TEST:
		if (GETARG_C(instr) ? isfalse(ra) : !isfalse(ra))
			ci->u.l.savedpc++;
		else
			donextjump(ci);
		break;
	case OP_TESTSET: {
		Tvalue *rb = RB(instr);
		if (GETARG_C(instr) ? isfalse(rb) : !isfalse(rb))
			ci->u.l.savedpc++;
		else {
			setobj(ks, ra, rb);
			donextjump(ci);
		}
		break;
		}
	case OP_CALL: {
		int b = GETARG_B(instr);
		nresults = GETARG_C(instr) - 1;

		if (b != 0)
			ks->top = ra + b;

		if (precall(ks, ra, nresults)) { /* C function */
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
			/* tail call: put called frame (n) in place of caller one (o) */
			Callinfo *nci = ks->ci;  /* called frame */
			Callinfo *oci = nci->prev;  /* caller frame */
			StkId nfunc = nci->func;  /* called function */
			StkId ofunc = oci->func;  /* caller function */
			/* last stack slot filled by 'precall' */
			StkId lim = nci->u.l.base + CLVALUE(nfunc)->p->numparams;

			int aux;
			/* close all upvalues from previous call */
			if (cl->p->sizep > 0)
				function_close(ks, oci->u.l.base);

			/* move new frame into old one */
			for (aux = 0; nfunc + aux < lim; aux++)
				setobj(ks, ofunc + aux, nfunc + aux);
			oci->u.l.base = ofunc + (nci->u.l.base - nfunc);  /* correct base */
			oci->top = ks->top = ofunc + (ks->top - nfunc);  /* correct top */
			oci->u.l.savedpc = nci->u.l.savedpc;
			//oci->callstatus |= CIST_TAIL;  /* function was tail called */
			ci = ks->ci = oci;  /* remove new frame */
			goto newframe;  /* restart ktap_execute over new ktap function */
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
		ktap_Number step = nvalue(ra+2);
		ktap_Number idx = NUMADD(nvalue(ra), step); /* increment index */
		ktap_Number limit = nvalue(ra+1);
		if (NUMLT(0, step) ? NUMLE(idx, limit) : NUMLE(limit, idx)) {
			ci->u.l.savedpc += GETARG_sBx(instr);  /* jump back */
			setnvalue(ra, idx);  /* update internal index... */
			setnvalue(ra+3, idx);  /* ...and external index */
		}
		break;
		}
	case OP_FORPREP: {
		const Tvalue *init = ra;
		const Tvalue *plimit = ra + 1;
		const Tvalue *pstep = ra + 2;

		if (!ktap_tonumber(init, ra))
			ktap_runerror(ks, KTAP_QL("for") " initial value must be a number");
		else if (!ktap_tonumber(plimit, ra + 1))
			ktap_runerror(ks, KTAP_QL("for") " limit must be a number");
		else if (!ktap_tonumber(pstep, ra + 2))
			ktap_runerror(ks, KTAP_QL("for") " step must be a number");

		setnvalue(ra, NUMSUB(nvalue(ra), nvalue(pstep)));
		ci->u.l.savedpc += GETARG_sBx(instr);
		break;
		}
	case OP_TFORCALL: {
		StkId cb = ra + 3;  /* call base */
		setobj(ks, cb + 2, ra + 2);
		setobj(ks, cb + 1, ra + 1);
		setobj(ks, cb, ra);
		ks->top = cb + 3;  /* func. + 2 args (state and index) */
		ktap_call(ks, cb, GETARG_C(instr));
		base = ci->u.l.base;
		ks->top = ci->top;
		instr = *(ci->u.l.savedpc++);  /* go to next instruction */
		ra = RA(instr);
		}
		/*go through */
	case OP_TFORLOOP:
		if (!ttisnil(ra + 1)) {  /* continue loop? */
			setobj(ks, ra, ra + 1);  /* save control variable */
			ci->u.l.savedpc += GETARG_sBx(instr);  /* jump back */
		}
		break;
	case OP_SETLIST: {
		int n = GETARG_B(instr);
		int c = GETARG_C(instr);
		int last;
		Table *h;

		if (n == 0)
			n = (int)(ks->top - ra) - 1;
		if (c == 0)
			c = GETARG_Ax(*ci->u.l.savedpc++);

		h = hvalue(ra);
		last = ((c - 1) * LFIELDS_PER_FLUSH) + n;
		if (last > h->sizearray)  /* needs more space? */
			table_resizearray(ks, h, last);  /* pre-allocate it at once */

		for (; n > 0; n--) {
			Tvalue *val = ra+n;
			table_setint(ks, h, last--, val);
		}
		ks->top = ci->top;  /* correct top (in case of previous open call) */
		break;
		}
	case OP_CLOSURE: {
		/* need to use closure cache? (multithread contention issue)*/
		Proto *p = cl->p->p[GETARG_Bx(instr)];
		pushclosure(ks, p, cl->upvals, base, ra);
		//checkGC(ks, ra + 1);
		break;
		}
	case OP_VARARG: {
		int b = GETARG_B(instr) - 1;
		int j;
		int n = (int)(base - ci->func) - cl->p->numparams - 1;
		if (b < 0) {  /* B == 0? */
			b = n;  /* get all var. arguments */
			checkstack(ks, n);
			ra = RA(instr);  /* previous call may change the stack */
			ks->top = ra + n;
		}
		for (j = 0; j < b; j++) {
			if (j < n) {
				setobj(ks, ra + j, base - n + j);
			} else
				setnilvalue(ra + j);
		}
		break;
		}
	case OP_EXTRAARG:
		return;

	case OP_EVENT: {
		Tvalue *b = RB(instr);
		if ((GETARG_B(instr) == 0) && ttisevent(b)) {
			ktap_event_handle(ks, evalue(b), GETARG_C(instr), ra);
		} else {
			/* normal OP_GETTABLE operation */
			setsvalue(RKC(instr), ktap_event_get_ts(ks, GETARG_C(instr)));

			gettable(ks, RB(instr), RKC(instr), ra);
			base = ci->u.l.base;
		}
		break;
		}
	}

	goto mainloop;
}

void ktap_call(ktap_State *ks, StkId func, int nresults)
{
	if (!precall(ks, func, nresults))
		ktap_execute(ks);
}

/*
 * making OP_EVENT for fast event field getting.
 *
 * This function must be called before all code loaded.
 */
void ktap_optimize_code(ktap_State *ks, int level, Proto *f)
{
	int i;

	for (i = 0; i < f->sizecode; i++) {
		int instr = f->code[i];
		Tvalue *k = f->k;

		switch (GET_OPCODE(instr)) {
		case OP_GETTABLE:
			if ((GETARG_B(instr) == 0) && ISK(GETARG_C(instr))) {
				Tvalue *field = k + INDEXK(GETARG_C(instr));
				if (ttype(field) == KTAP_TSTRING) {
					int index = ktap_event_get_index(svalue(field));
					if (index == -1)
						break;

					SET_OPCODE(instr, OP_EVENT);
					SETARG_C(instr, index);
					f->code[i] = instr;
					break;
				}
			}
		}
	}

	/* continue optimize sub functions */
	for (i = 0; i < f->sizep; i++)
		ktap_optimize_code(ks, level + 1, f->p[i]);
}


/* function for register library */
void ktap_register_lib(ktap_State *ks, const char *libname, const ktap_Reg *funcs)
{
	int i;
	Table *target_tbl;
	const Tvalue *gt = table_getint(hvalue(&G(ks)->registry), KTAP_RIDX_GLOBALS);

	/* lib is null when register baselib function */
	if (libname == NULL)
		target_tbl = hvalue(gt);
	else {
		Tvalue key, val;

		target_tbl = table_new(ks);
		table_resize(ks, target_tbl, 0, sizeof(*funcs) / sizeof(ktap_Reg));

		setsvalue(&key, tstring_new(ks, libname));
		sethvalue(&val, target_tbl);
		table_setvalue(ks, hvalue(gt), &key, &val);
	}

	for (i = 0; funcs[i].name != NULL; i++) {
		Tvalue func_name, cl;

		setsvalue(&func_name, tstring_new(ks, funcs[i].name));
		setfvalue(&cl, funcs[i].func);
		table_setvalue(ks, target_tbl, &func_name, &cl);
	}
}

#define BASIC_STACK_SIZE        (2 * KTAP_MINSTACK)

static void ktap_init_registry(ktap_State *ks)
{
	Tvalue mt;
	Table *registry = table_new(ks);

	sethvalue(&G(ks)->registry, registry);
	table_resize(ks, registry, KTAP_RIDX_LAST, 0);
	setthvalue(ks, &mt, ks);
	table_setint(ks, registry, KTAP_RIDX_MAINTHREAD, &mt);
	setthvalue(ks, &mt, table_new(ks));
	table_setint(ks, registry, KTAP_RIDX_GLOBALS, &mt);
}

static void ktap_init_state(ktap_State *ks)
{
	Callinfo *ci;
	int i;

	ks->stack = ktap_malloc(ks, BASIC_STACK_SIZE * sizeof(Tvalue));
	if (!ks->stack) {
		printk("cannot alloc stack %d\n", BASIC_STACK_SIZE * sizeof(Tvalue));	
		dump_stack();
		return;
	}
	ks->stacksize = BASIC_STACK_SIZE;

	for (i = 0; i < BASIC_STACK_SIZE; i++)
		setnilvalue(ks->stack + i);

	ks->top = ks->stack;
	ks->stack_last = ks->stack + ks->stacksize;

	ci = &ks->baseci;
	ci->next = ci->prev = NULL;
	ci->callstatus = 0;
	ci->func = ks->top;
	setnilvalue(ks->top++);
	ci->top = ks->top + KTAP_MINSTACK;
	ks->ci = ci;
}

void ktap_exitthread(ktap_State *ks)
{
	free_ci(ks);
	ktap_free(ks, ks->stack);

	if (ks == G(ks)->mainthread)
		ktap_free(ks, ks);
}

static ktap_State *ktap_percpu_state;
ktap_State *ktap_newthread(ktap_State *mainthread)
{
	ktap_State *ks;
	global_State *g = G(mainthread);

	if (mainthread != g->mainthread)
		return NULL;

	ks = per_cpu_ptr(ktap_percpu_state, smp_processor_id());
	G(ks) = G(mainthread);
	ktap_init_state(ks);
	return ks;
}

/* todo: move this ktap_exit to other location */
/* todo: how to process not-mainthread exit? */
void ktap_exit(ktap_State *ks)
{
	if (ks != G(ks)->mainthread) {
		ktap_printf(ks, "Error: ktap_exit called by non mainthread\n");
		return;
	}
	/* we need to flush all signals, otherwise cannot print to file */
	flush_signals(current);

	ktap_printf(ks, "exitting\n");
	end_all_trace(ks);

	free_percpu(ktap_percpu_state);

	/* free all resources got by ktap */
	tstring_freeall(ks);
	free_all_gcobject(ks);
	ktap_exitthread(ks);

	ktap_transport_exit();

	/* life ending, no return anymore*/
	do_exit(0);
}

/* ktap mainthread initization, main entry for ktap */
ktap_State *ktap_newstate()
{
	struct fd f;
	ktap_State *ks;
	int ret;

	ks = kzalloc(sizeof(ktap_State) + sizeof(global_State), GFP_KERNEL);
	if (!ks)
		return NULL;

	G(ks) = (global_State *)(ks + 1);
	G(ks)->mainthread = ks;
	G(ks)->seed = 201236; /* todo: make more random in */
	G(ks)->task = current;
	INIT_LIST_HEAD(&(G(ks)->event_nodes));

	ret = ktap_transport_init();
	if (ret)
		return NULL;

	ktap_trace_init();

	/* init output file structure, use for ktap_printf*/
	f = fdget(1);
	G(ks)->ofile = f.file;
	fdput(f);

	ktap_init_state(ks);
	ktap_init_registry(ks);
	tstring_resize(ks, 32); /* set inital string hashtable size */

	/* init library */
	ktap_init_syscalls(ks);
	ktap_init_baselib(ks);
	ktap_init_oslib(ks);

	ktap_percpu_state = alloc_percpu(PAGE_SIZE);
	if (!ktap_percpu_state)
		return NULL;

	return ks;
}


