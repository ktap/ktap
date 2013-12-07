/*
 * kp_obj.c - ktap object generic operation
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

#include "../include/ktap_types.h"
#include "../include/ktap_ffi.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_tab.h"

#ifdef __KERNEL__
#include <linux/slab.h>
#include "ktap.h"
#include "kp_vm.h"
#include "kp_transport.h"

#define KTAP_ALLOC_FLAGS ((GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN) \
			 & ~__GFP_WAIT)

void *kp_malloc(ktap_state *ks, int size)
{
	void *addr;

	/*
	 * Normally we don't want to trace under memory pressure,
	 * so we use a simple rule to handle memory allocation failure:
	 *
	 * retry until allocation success, this will make caller don't need
	 * to handle the unlikely failure case, then ktap exit.
	 *
	 * In this approach, if user find there have memory allocation failure,
	 * user should re-run the ktap script, or fix the memory pressure
	 * issue, or figure out why the script need so many memory.
	 *
	 * Perhaps return pre-allocated stub memory trunk when allocate failed
	 * is a better approch?
	 */
	addr = kmalloc(size, KTAP_ALLOC_FLAGS);
	if (unlikely(!addr)) {
		kp_error(ks, "kmalloc size %d failed, retry again\n", size);
		printk("ktap kmalloc size %d failed, retry again\n", size);
		dump_stack();
		while (1) {
			addr = kmalloc(size, KTAP_ALLOC_FLAGS);
			if (addr)
				break;
		}
		kp_printf(ks, "kmalloc retry success after failed, exit\n");
	}

	preempt_disable();
	KTAP_STATS(ks)->nr_mem_allocate += 1;
	KTAP_STATS(ks)->mem_allocated += size;
	preempt_enable();

	return addr;
}

void kp_free(ktap_state *ks, void *addr)
{
	preempt_disable();
	KTAP_STATS(ks)->nr_mem_free += 1;
	preempt_enable();

	kfree(addr);
}

void *kp_reallocv(ktap_state *ks, void *addr, int oldsize, int newsize)
{
	void *new_addr;

	new_addr = krealloc(addr, newsize, KTAP_ALLOC_FLAGS);
	if (unlikely(!new_addr)) {
		kp_error(ks, "krealloc size %d failed, retry again\n", newsize);
		printk("ktap krealloc size %d failed, retry again\n", newsize);
		dump_stack();
		while (1) {
			new_addr = krealloc(addr, newsize, KTAP_ALLOC_FLAGS);
			if (new_addr)
				break;
		}
		kp_printf(ks, "krealloc retry success after failed, exit\n");
	}

	preempt_disable();
	if (oldsize == 0) {
		KTAP_STATS(ks)->nr_mem_allocate += 1;
	}
	KTAP_STATS(ks)->mem_allocated += newsize - oldsize;
	preempt_enable();

	return new_addr;
}

void *kp_zalloc(ktap_state *ks, int size)
{
	void *addr;

	addr = kzalloc(size, KTAP_ALLOC_FLAGS);
	if (unlikely(!addr)) {
		kp_error(ks, "kzalloc size %d failed, retry again\n", size);
		printk("ktap kzalloc size %d failed, retry again\n", size);
		dump_stack();
		while (1) {
			addr = kzalloc(size, KTAP_ALLOC_FLAGS);
			if (addr)
				break;
		}
		kp_printf(ks, "kzalloc retry success after failed, exit\n");
	}

	preempt_disable();
	KTAP_STATS(ks)->nr_mem_allocate += 1;
	KTAP_STATS(ks)->mem_allocated += size;
	preempt_enable();

	return addr;
}
#endif

void kp_obj_dump(ktap_state *ks, const ktap_value *v)
{
	switch (ttype(v)) {
	case KTAP_TNIL:
		kp_puts(ks, "NIL");
		break;
	case KTAP_TNUMBER:
		kp_printf(ks, "NUMBER %ld", nvalue(v));
		break;
	case KTAP_TBOOLEAN:
		kp_printf(ks, "BOOLEAN %d", bvalue(v));
		break;
	case KTAP_TLIGHTUSERDATA:
		kp_printf(ks, "LIGHTUSERDATA 0x%lx", (unsigned long)pvalue(v));
		break;
	case KTAP_TCFUNCTION:
		kp_printf(ks, "LIGHTCFCUNTION 0x%lx", (unsigned long)fvalue(v));
		break;
	case KTAP_TSHRSTR:
	case KTAP_TLNGSTR:
		kp_printf(ks, "SHRSTR #%s", svalue(v));
		break;
	case KTAP_TTABLE:
		kp_printf(ks, "TABLE 0x%lx", (unsigned long)hvalue(v));
		break;
        default:
		kp_printf(ks, "GCVALUE 0x%lx", (unsigned long)gcvalue(v));
		break;
	}
}

#ifdef __KERNEL__
#include <linux/stacktrace.h>
#include <linux/module.h>
#include <linux/kallsyms.h>

static void kp_btrace_dump(ktap_state *ks, ktap_btrace *bt)
{
	char str[KSYM_SYMBOL_LEN];
	unsigned long *entries = (unsigned long *)(bt + 1);
	int i;

	for (i = 0; i < bt->nr_entries; i++) {
		unsigned long p = entries[i];

		if (p == ULONG_MAX)
			break;

		SPRINT_SYMBOL(str, p);
		kp_printf(ks, "%s\n", str);
	}
}

static int kp_btrace_equal(ktap_btrace *bt1, ktap_btrace *bt2)
{
	unsigned long *entries1 = (unsigned long *)(bt1 + 1);
	unsigned long *entries2 = (unsigned long *)(bt2 + 1);
	int i;

	if (bt1->nr_entries != bt2->nr_entries)
		return 0;

	for (i = 0; i < bt1->nr_entries; i++) {
		if (entries1[i] != entries2[i])
			return 0;
	}

	return 1;
}
#endif

void kp_showobj(ktap_state *ks, const ktap_value *v)
{
	switch (ttype(v)) {
	case KTAP_TNIL:
		kp_puts(ks, "nil");
		break;
	case KTAP_TNUMBER:
		kp_printf(ks, "%ld", nvalue(v));
		break;
	case KTAP_TBOOLEAN:
		kp_puts(ks, (bvalue(v) == 1) ? "true" : "false");
		break;
	case KTAP_TLIGHTUSERDATA:
		kp_printf(ks, "0x%lx", (unsigned long)pvalue(v));
		break;
	case KTAP_TCFUNCTION:
		kp_printf(ks, "0x%lx", (unsigned long)fvalue(v));
		break;
	case KTAP_TSHRSTR:
	case KTAP_TLNGSTR:
		kp_puts(ks, svalue(v));
		break;
	case KTAP_TTABLE:
		kp_tab_dump(ks, hvalue(v));
		break;
#ifdef __KERNEL__
#ifdef CONFIG_KTAP_FFI
	case KTAP_TCDATA:
		kp_cdata_dump(ks, cdvalue(v));
		break;
#endif
	case KTAP_TEVENT:
		kp_transport_event_write(ks, evalue(v));
		break;
	case KTAP_TBTRACE:
		kp_btrace_dump(ks, btvalue(v));
		break;
	case KTAP_TPTABLE:
		kp_ptab_dump(ks, phvalue(v));
		break;
	case KTAP_TSTATDATA:
		kp_statdata_dump(ks, sdvalue(v));
		break;
#endif
        default:
		kp_error(ks, "print unknown value type: %d\n", ttype(v));
		break;
	}
}


/*
 * equality of ktap values. ks == NULL means raw equality
 */
int kp_equalobjv(ktap_state *ks, const ktap_value *t1, const ktap_value *t2)
{
	switch (ttype(t1)) {
	case KTAP_TNIL:
		return 1;
	case KTAP_TNUMBER:
		return nvalue(t1) == nvalue(t2);
	case KTAP_TBOOLEAN:
		return bvalue(t1) == bvalue(t2);  /* true must be 1 !! */
	case KTAP_TLIGHTUSERDATA:
		return pvalue(t1) == pvalue(t2);
	case KTAP_TCFUNCTION:
		return fvalue(t1) == fvalue(t2);
	case KTAP_TSHRSTR:
		return eqshrstr(rawtsvalue(t1), rawtsvalue(t2));
	case KTAP_TLNGSTR:
		return kp_tstring_eqlngstr(rawtsvalue(t1), rawtsvalue(t2));
	case KTAP_TTABLE:
		if (hvalue(t1) == hvalue(t2))
			return 1;
		else if (ks == NULL)
			return 0;
#ifdef __KERNEL__
	case KTAP_TBTRACE:
		return kp_btrace_equal(btvalue(t1), btvalue(t2));
#endif
	default:
		return gcvalue(t1) == gcvalue(t2);
	}

	return 0;
}

/*
 * ktap will not use lua's length operator on table meaning,
 * also # is not for length operator any more in ktap.
 */
int kp_objlen(ktap_state *ks, const ktap_value *v)
{
	switch(v->type) {
	case KTAP_TTABLE:
		return kp_tab_length(ks, hvalue(v));
	case KTAP_TSTRING:
		return rawtsvalue(v)->tsv.len;
	default:
		kp_printf(ks, "cannot get length of type %d\n", v->type);
		return -1;
	}
	return 0;
}

/* need to protect allgc field? */
ktap_gcobject *kp_newobject(ktap_state *ks, int type, size_t size,
			    ktap_gcobject **list)
{
	ktap_gcobject *o;

	o = kp_malloc(ks, size);
	if (list == NULL)
		list = &G(ks)->allgc;

	gch(o)->tt = type;
	gch(o)->next = *list;
	*list = o;

	return o;
}

ktap_upval *kp_newupval(ktap_state *ks)
{
	ktap_upval *uv;

	uv = &kp_newobject(ks, KTAP_TUPVAL, sizeof(ktap_upval), NULL)->uv;
	uv->v = &uv->u.value;
	set_nil(uv->v);
	return uv;
}

static ktap_btrace *kp_newbacktrace(ktap_state *ks, int nr_entries,
				    ktap_gcobject **list)
{
	ktap_btrace *bt;
	int size = sizeof(ktap_btrace) + nr_entries * sizeof(unsigned long);

	bt = &kp_newobject(ks, KTAP_TBTRACE, size, list)->bt;
	bt->nr_entries = nr_entries;
	return bt;
}

void kp_objclone(ktap_state *ks, const ktap_value *o, ktap_value *newo,
		 ktap_gcobject **list)
{
	if (is_btrace(o)) {
		int nr_entries = btvalue(o)->nr_entries;
		ktap_btrace *bt;

		bt = kp_newbacktrace(ks, nr_entries, list);
		memcpy((unsigned long *)(bt + 1), btvalue(o) + 1,
			nr_entries * sizeof(unsigned long));
		set_btrace(newo, bt);
	} else {
		kp_error(ks, "cannot clone ktap value type %d\n", ttype(o));
		set_nil(newo);
	}
}

ktap_closure *kp_newclosure(ktap_state *ks, int n)
{
	ktap_closure *cl;

	cl = (ktap_closure *)kp_newobject(ks, KTAP_TCLOSURE, sizeof(*cl), NULL);
	cl->p = NULL;
	cl->nupvalues = n;
	while (n--)
		cl->upvals[n] = NULL;

	return cl;
}

static void free_proto(ktap_state *ks, ktap_proto *f)
{
	kp_free(ks, f->code);
	kp_free(ks, f->p);
	kp_free(ks, f->k);
	kp_free(ks, f->lineinfo);
	kp_free(ks, f->locvars);
	kp_free(ks, f->upvalues);
	kp_free(ks, f);
}

ktap_proto *kp_newproto(ktap_state *ks)
{
	ktap_proto *f;
	f = (ktap_proto *)kp_newobject(ks, KTAP_TPROTO, sizeof(*f), NULL);
	f->k = NULL;
 	f->sizek = 0;
	f->p = NULL;
	f->sizep = 0;
	f->code = NULL;
	f->cache = NULL;
	f->sizecode = 0;
	f->lineinfo = NULL;
	f->sizelineinfo = 0;
	f->upvalues = NULL;
	f->sizeupvalues = 0;
	f->numparams = 0;
	f->is_vararg = 0;
	f->maxstacksize = 0;
	f->locvars = NULL;
	f->sizelocvars = 0;
	f->linedefined = 0;
	f->lastlinedefined = 0;
	f->source = NULL;
	return f;
}

void kp_free_gclist(ktap_state *ks, ktap_gcobject *o)
{
	while (o) {
		ktap_gcobject *next;

		next = gch(o)->next;
		switch (gch(o)->tt) {
		case KTAP_TTABLE:
			kp_tab_free(ks, (ktap_tab *)o);
			break;
		case KTAP_TPROTO:
			free_proto(ks, (ktap_proto *)o);
			break;
#ifdef __KERNEL__
		case KTAP_TPTABLE:
			kp_ptab_free(ks, (ktap_ptab *)o);
			break;
#endif
		default:
			kp_free(ks, o);
		}
		o = next;
	}
}

void kp_free_all_gcobject(ktap_state *ks)
{
	kp_free_gclist(ks, G(ks)->allgc);
	G(ks)->allgc = NULL;
}

/******************************************************************************/

/*
 * make header for precompiled chunks
 * if you change the code below be sure to update load_header and FORMAT above
 * and KTAPC_HEADERSIZE in ktap_types.h
 */
void kp_header(u8 *h)
{
	int x = 1;

	memcpy(h, KTAP_SIGNATURE, sizeof(KTAP_SIGNATURE) - sizeof(char));
	h += sizeof(KTAP_SIGNATURE) - sizeof(char);
	*h++ = (u8)VERSION;
	*h++ = (u8)FORMAT;
	*h++ = (u8)(*(char*)&x);                    /* endianness */
	*h++ = (u8)(sizeof(int));
	*h++ = (u8)(sizeof(size_t));
	*h++ = (u8)(sizeof(ktap_instruction));
	*h++ = (u8)(sizeof(ktap_number));
	*h++ = (u8)(((ktap_number)0.5) == 0); /* is ktap_number integral? */
	memcpy(h, KTAPC_TAIL, sizeof(KTAPC_TAIL) - sizeof(char));
}


