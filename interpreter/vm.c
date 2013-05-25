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
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/freezer.h>
#include "../include/ktap.h"

#define KTAP_MINSTACK 20

/* todo: enlarge maxstack for big system like 64-bit */
#define KTAP_MAXSTACK           15000

#define CIST_KTAP	(1 << 0) /* call is running a ktap function */
#define CIST_REENTRY	(1 << 2)

#define isktapfunc(ci)	((ci)->callstatus & CIST_KTAP)

void kp_call(ktap_State *ks, StkId func, int nresults);

void ktap_concat(ktap_State *ks, int start, int end)
{
	int i, len = 0;
	StkId top = ks->ci->u.l.base;
	Tstring *ts;
	char *ptr, *buffer;

	for (i = start; i <= end; i++) {
		if (!ttisstring(top + i)) {
			kp_printf(ks, "cannot concat non-string\n");
			return;
		}

		len += rawtsvalue(top + i)->tsv.len;
	}
	if (len <= sizeof(*ks->buff)) {
		buffer = ks->buff;
	} else
		buffer = kp_malloc(ks, len);
	ptr = buffer;

	for (i = start; i <= end; i++) {
		int len = rawtsvalue(top + start)->tsv.len;
		strncpy(ptr, svalue(top + i), len);
		ptr += len;
	}
	ts = kp_tstring_newlstr(ks, buffer, len);
	setsvalue(top + start, ts);

	if (buffer != ks->buff)
		kp_free(ks, buffer);
}

/* todo: compare l == r if both is tstring type? */
static int lessthan(ktap_State *ks, const Tvalue *l, const Tvalue *r)
{
	if (ttisnumber(l) && ttisnumber(r))
		return NUMLT(nvalue(l), nvalue(r));
	else if (ttisstring(l) && ttisstring(r))
		return kp_tstring_cmp(rawtsvalue(l), rawtsvalue(r)) < 0;

	return 0;
}

static int lessequal(ktap_State *ks, const Tvalue *l, const Tvalue *r)
{
	if (ttisnumber(l) && ttisnumber(r))
		return NUMLE(nvalue(l), nvalue(r));
	else if (ttisstring(l) && ttisstring(r))
		return kp_tstring_cmp(rawtsvalue(l), rawtsvalue(r)) <= 0;

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
	uv = &kp_newobject(ks, KTAP_TUPVAL, sizeof(Upval), pp)->uv;
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
	Closure *ncl = kp_newlclosure(ks, nup);

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
	setobj(ks, val, kp_table_get(hvalue(t), key));
}

static void settable(ktap_State *ks, const Tvalue *t, Tvalue *key, StkId val)
{
	kp_table_setvalue(ks, hvalue(t), key, val);
}

static void growstack(ktap_State *ks, int n)
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
		kp_runerror(ks, "stack overflow");
		return;
	}

	/* realloc stack */
	oldstack = ks->stack;
	lim = ks->stacksize;
	kp_realloc(ks, ks->stack, ks->stacksize, newsize, Tvalue);

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

	ci = kp_malloc(ks, sizeof(Callinfo));
	ks->ci->next = ci;
	ci->prev = ks->ci;
	ci->next = NULL;

	return ci;
}

static void free_ci(ktap_State *ks)
{
	Callinfo *ci = ks->ci;
	Callinfo *next;

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
	default:
		kp_printf(ks, "cannot calling nil function\n");
		return -1;
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

static Tvalue *cfunction_cache_get(ktap_State *ks, int index);

static void ktap_execute(ktap_State *ks)
{
	int exec_count = 0;
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

	/* dead loop detaction */
	if (exec_count++ == 10000) {
		if (G(ks)->mainthread != ks) {
			pr_warn("non-mainthread executing too much, please "
				"try to enlarge execution limit\n");
			return;
		}

		cond_resched();
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

//	kp_printf(ks, "execute instruction: %s\n", ktap_opnames[opcode]);

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
		Table *t = kp_table_new(ks);
		sethvalue(ra, t);
		if (b != 0 || c != 0)
			kp_table_resize(ks, t, fb2int(b), fb2int(c));
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
		/* divide 0 checking */
		if (!nvalue(RKC(instr))) {
			kp_printf(ks, "error: divide 0 arith operation, exit\n");
			return;
		}
		arith_op(NUMDIV);
		break;
	case OP_MOD:
		/* divide 0 checking */
		if (!nvalue(RKC(instr))) {
			kp_printf(ks, "error: mod 0 arith operation, exit\n");
			return;
		}
		arith_op(NUMMOD);
		break;
	case OP_POW:
		kp_printf(ks, "ktap don't support pow arith in kernel, exit\n");
		return;
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
	case OP_LEN: {
		int len = kp_objlen(ks, RB(instr));
		if (len < 0)
			return;
		setnvalue(ra, len);
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
		int ret;

		nresults = GETARG_C(instr) - 1;

		if (b != 0)
			ks->top = ra + b;

		ret = precall(ks, ra, nresults);
		if (ret == -1)
			return;

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
			kp_runerror(ks, KTAP_QL("for") " initial value must be a number");
		else if (!ktap_tonumber(plimit, ra + 1))
			kp_runerror(ks, KTAP_QL("for") " limit must be a number");
		else if (!ktap_tonumber(pstep, ra + 2))
			kp_runerror(ks, KTAP_QL("for") " step must be a number");

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
		kp_call(ks, cb, GETARG_C(instr));
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
			kp_table_resizearray(ks, h, last);  /* pre-allocate it at once */

		for (; n > 0; n--) {
			Tvalue *val = ra+n;
			kp_table_setint(ks, h, last--, val);
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
			kp_event_handle(ks, evalue(b), GETARG_C(instr), ra);
		} else {
			/* normal OP_GETTABLE operation */
			setsvalue(RKC(instr), kp_event_get_ts(ks, GETARG_C(instr)));

			gettable(ks, RB(instr), RKC(instr), ra);
			base = ci->u.l.base;
		}
		break;
		}

	case OP_LOAD_GLOBAL: {
		Tvalue *cfunc = cfunction_cache_get(ks, GETARG_C(instr));
		setobj(ks, ra, cfunc);
		}
		break;
	}

	goto mainloop;
}

void kp_call(ktap_State *ks, StkId func, int nresults)
{
	if (!precall(ks, func, nresults))
		ktap_execute(ks);
}

static int cfunction_cache_getindex(ktap_State *ks, Tvalue *fname);

/*
 * making OP_EVENT for fast event field getting.
 *
 * This function must be called before all code loaded.
 */
void kp_optimize_code(ktap_State *ks, int level, Proto *f)
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
					int index = kp_event_get_index(svalue(field));
					if (index == -1)
						break;

					SET_OPCODE(instr, OP_EVENT);
					SETARG_C(instr, index);
					f->code[i] = instr;
					break;
				}
			}
			break;
		case OP_GETTABUP:
			if ((GETARG_B(instr) == 0) && ISK(GETARG_C(instr))) {
				Tvalue *field = k + INDEXK(GETARG_C(instr));
				if (ttype(field) == KTAP_TSTRING) {
					int index = cfunction_cache_getindex(ks, field);
					if (index == -1)
						break;

					SET_OPCODE(instr, OP_LOAD_GLOBAL);
					SETARG_C(instr, index);
					f->code[i] = instr;
					break;
				}
			}
			break;
		}
	}

	/* continue optimize sub functions */
	for (i = 0; i < f->sizep; i++)
		kp_optimize_code(ks, level + 1, f->p[i]);
}

static Tvalue *cfunction_cache_get(ktap_State *ks, int index)
{
	return &G(ks)->cfunction_tbl[index];
}

static int cfunction_cache_getindex(ktap_State *ks, Tvalue *fname)
{
	const Tvalue *gt = kp_table_getint(hvalue(&G(ks)->registry),
				KTAP_RIDX_GLOBALS);
	Tvalue *cfunc;
	int nr, i;

	nr = G(ks)->nr_builtin_cfunction;
	cfunc = kp_table_get(hvalue(gt), fname);

	for (i = 0; i < nr; i++) {
		if (rawequalobj(&G(ks)->cfunction_tbl[i], cfunc))
			return i;
	}

	return -1;
}

static void cfunction_cache_add(ktap_State *ks, Tvalue *func)
{
	int nr = G(ks)->nr_builtin_cfunction;
	setobj(ks, &G(ks)->cfunction_tbl[nr], func);
	G(ks)->nr_builtin_cfunction++;
}

static void cfunction_cache_exit(ktap_State *ks)
{
	kp_free(ks, G(ks)->cfunction_tbl);
}

static int cfunction_cache_init(ktap_State *ks)
{
	G(ks)->cfunction_tbl = kp_zalloc(ks, sizeof(Tvalue) * 128);
	if (!G(ks)->cfunction_tbl)
		return -ENOMEM;

	return 0;
}

/* function for register library */
void kp_register_lib(ktap_State *ks, const char *libname, const ktap_Reg *funcs)
{
	int i;
	Table *target_tbl;
	const Tvalue *gt = kp_table_getint(hvalue(&G(ks)->registry), KTAP_RIDX_GLOBALS);

	/* lib is null when register baselib function */
	if (libname == NULL)
		target_tbl = hvalue(gt);
	else {
		Tvalue key, val;

		target_tbl = kp_table_new(ks);
		kp_table_resize(ks, target_tbl, 0, sizeof(*funcs) / sizeof(ktap_Reg));

		setsvalue(&key, kp_tstring_new(ks, libname));
		sethvalue(&val, target_tbl);
		kp_table_setvalue(ks, hvalue(gt), &key, &val);
	}

	for (i = 0; funcs[i].name != NULL; i++) {
		Tvalue func_name, cl;

		setsvalue(&func_name, kp_tstring_new(ks, funcs[i].name));
		setfvalue(&cl, funcs[i].func);
		kp_table_setvalue(ks, target_tbl, &func_name, &cl);

		cfunction_cache_add(ks, &cl);
	}
}

#define BASIC_STACK_SIZE        (2 * KTAP_MINSTACK)

static void ktap_init_registry(ktap_State *ks)
{
	Tvalue mt;
	Table *registry = kp_table_new(ks);

	sethvalue(&G(ks)->registry, registry);
	kp_table_resize(ks, registry, KTAP_RIDX_LAST, 0);
	setthvalue(ks, &mt, ks);
	kp_table_setint(ks, registry, KTAP_RIDX_MAINTHREAD, &mt);
	setthvalue(ks, &mt, kp_table_new(ks));
	kp_table_setint(ks, registry, KTAP_RIDX_GLOBALS, &mt);
}

static void ktap_init_arguments(ktap_State *ks, int argc, char **argv)
{
	const Tvalue *gt = kp_table_getint(hvalue(&G(ks)->registry),
			   KTAP_RIDX_GLOBALS);
	Table *global_tbl = hvalue(gt);
	Table *arg_tbl = kp_table_new(ks);
	Tvalue arg_tblval;
	Tvalue arg_tsval;
	int i;
	
	setsvalue(&arg_tsval, kp_tstring_new(ks, "arg"));
	sethvalue(&arg_tblval, arg_tbl);
	setobj(ks, kp_table_set(ks, global_tbl, &arg_tsval), &arg_tblval);

	if (!argc)
		return;

	kp_table_resize(ks, arg_tbl, 100, 100);

	for (i = 0; i < argc; i++) {
		int res;
		Tvalue val;

		if (!kstrtoint(argv[i], 10, &res)) {
			setnvalue(&val, res);
		} else
			setsvalue(&val, kp_tstring_new(ks, argv[i]));

		kp_table_setint(ks, arg_tbl, i, &val);
	}
}


#define KTAP_STACK_SIZE (BASIC_STACK_SIZE * sizeof(Tvalue))
static void *percpu_ktap_stack;

static void ktap_init_state(ktap_State *ks)
{
	Callinfo *ci;
	int i;

	if (ks == G(ks)->mainthread) {
		ks->stack = kp_malloc(ks, KTAP_STACK_SIZE);
		if (!ks->stack) {
			kp_printf(ks, "cannot alloc stack %d\n",
				    KTAP_STACK_SIZE);
			return;
		}
	} else {
		ks->stack = per_cpu_ptr(percpu_ktap_stack, smp_processor_id());
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

void kp_exitthread(ktap_State *ks)
{
	Gcobject *o = ks->localgc;
	Gcobject *next;

	free_ci(ks);

	if (ks == G(ks)->mainthread)
		kp_free(ks, ks->stack);

	/* free local allocation objects, like annotate strings */
	while (o) {
		next = gch(o)->next;
		kp_free(ks, o);
		o = next;
	}
}

static ktap_State *ktap_percpu_state;
ktap_State *kp_newthread(ktap_State *mainthread)
{
	ktap_State *ks;
	global_State *g = G(mainthread);

	if (mainthread != g->mainthread)
		return NULL;

	ks = per_cpu_ptr(ktap_percpu_state, smp_processor_id());
	G(ks) = G(mainthread);
	ks->localgc = NULL;
	ktap_init_state(ks);
	return ks;
}

void kp_user_complete(ktap_State *ks)
{
	if (!ks || !G(ks)->user_completion)
		return;

	complete(G(ks)->user_completion);
	G(ks)->user_completion = NULL;
}

static int wait_user_completion(ktap_State *ks)
{
	struct completion t;
	int killed;

	G(ks)->user_completion = &t;
	init_completion(&t);

	freezer_do_not_count();
	killed = wait_for_completion_interruptible(&t);
	freezer_count();

	G(ks)->user_completion = NULL;

	return killed;
}


/* todo: how to process not-mainthread exit? */
void kp_exit(ktap_State *ks)
{
	if (ks != G(ks)->mainthread) {
		kp_printf(ks, "Error: ktap_exit called by non mainthread\n");
		return;
	}
	/* we need to flush all signals */
	flush_signals(current);

	kp_probe_exit(ks);
	kp_exit_timers(ks);

	free_percpu(ktap_percpu_state);
	free_percpu(percpu_ktap_stack);

	/* free all resources got by ktap */
	kp_tstring_freeall(ks);
	kp_free_all_gcobject(ks);
	cfunction_cache_exit(ks);

	wait_user_completion(ks);
	flush_signals(current);

	kp_transport_exit(ks);

	kp_exitthread(ks);
	kp_free(ks, ks);

	/* life ending, no return anymore*/
	do_exit(0);
}

/* ktap mainthread initization, main entry for ktap */
ktap_State *kp_newstate(ktap_State **private_data, int argc, char **argv)
{
	ktap_State *ks;
	int ret;

	ks = kzalloc(sizeof(ktap_State) + sizeof(global_State), GFP_KERNEL);
	if (!ks)
		return NULL;

	*private_data = ks;

	G(ks) = (global_State *)(ks + 1);
	G(ks)->mainthread = ks;
	G(ks)->seed = 201236; /* todo: make more random in */
	G(ks)->task = current;
	INIT_LIST_HEAD(&(G(ks)->timers));

	if (cfunction_cache_init(ks) < 0)
		return NULL;


	ret = kp_transport_init(ks);
	if (ret)
		return NULL;

	kp_tstring_resize(ks, 512); /* set inital string hashtable size */

	ktap_init_state(ks);
	ktap_init_registry(ks);
	ktap_init_arguments(ks, argc, argv);

	if (kp_probe_init(ks))
		return NULL;

	/* init library */
	kp_init_baselib(ks);
	kp_init_oslib(ks);
	kp_init_kdebuglib(ks);
	kp_init_timerlib(ks);

	ktap_percpu_state = (ktap_State *)alloc_percpu(ktap_State);
	if (!ktap_percpu_state)
		return NULL;

	percpu_ktap_stack = __alloc_percpu(KTAP_STACK_SIZE, __alignof__(char));
	if (!percpu_ktap_stack)
		return NULL;

	return ks;
}


