/*
 * kp_obj.c - ktap object generic operation
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2014 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
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

#include <linux/stacktrace.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/slab.h>
#include "../include/ktap_types.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_tab.h"
#include "ktap.h"
#include "kp_vm.h"
#include "kp_transport.h"

/* Error message strings. */
const char *kp_err_allmsg =
#define ERRDEF(name, msg)       msg "\0"
#include "../include/ktap_errmsg.h"
;

/* memory allocation flag */
#define KTAP_ALLOC_FLAGS ((GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN) \
			 & ~__GFP_WAIT)

void *kp_malloc(ktap_state_t *ks, int size)
{
	void *addr;

	addr = kmalloc(size, KTAP_ALLOC_FLAGS);
	if (unlikely(!addr)) {
		kp_error(ks, "kmalloc failed\n");
	}
	return addr;
}

void *kp_zalloc(ktap_state_t *ks, int size)
{
	void *addr;

	addr = kzalloc(size, KTAP_ALLOC_FLAGS);
	if (unlikely(!addr))
		kp_error(ks, "kzalloc failed\n");
	return addr;
}

void kp_free(ktap_state_t *ks, void *addr)
{
	kfree(addr);
}


void kp_obj_dump(ktap_state_t *ks, const ktap_val_t *v)
{
	switch (itype(v)) {
	case KTAP_TNIL:
		kp_puts(ks, "NIL");
		break;
	case KTAP_TTRUE:
		kp_printf(ks, "true");
		break;
	case KTAP_TFALSE:
		kp_printf(ks, "false");
		break;
	case KTAP_TNUM:
		kp_printf(ks, "NUM %ld", nvalue(v));
		break;
	case KTAP_TLIGHTUD:
		kp_printf(ks, "LIGHTUD 0x%lx", (unsigned long)pvalue(v));
		break;
	case KTAP_TFUNC:
		kp_printf(ks, "FUNCTION 0x%lx", (unsigned long)fvalue(v));
		break;
	case KTAP_TSTR:
		kp_printf(ks, "STR #%s", svalue(v));
		break;
	case KTAP_TTAB:
		kp_printf(ks, "TABLE 0x%lx", (unsigned long)hvalue(v));
		break;
        default:
		kp_printf(ks, "GCVALUE 0x%lx", (unsigned long)gcvalue(v));
		break;
	}
}

void kp_obj_show(ktap_state_t *ks, const ktap_val_t *v)
{
	switch (itype(v)) {
	case KTAP_TNIL:
		kp_puts(ks, "nil");
		break;
	case KTAP_TTRUE:
		kp_puts(ks, "true");
		break;
	case KTAP_TFALSE:
		kp_puts(ks, "false");
		break;
	case KTAP_TNUM:
		kp_printf(ks, "%ld", nvalue(v));
		break;
	case KTAP_TLIGHTUD:
		kp_printf(ks, "lightud 0x%lx", (unsigned long)pvalue(v));
		break;
	case KTAP_TCFUNC:
		kp_printf(ks, "cfunction 0x%lx", (unsigned long)fvalue(v));
		break;
	case KTAP_TFUNC:
		kp_printf(ks, "function 0x%lx", (unsigned long)gcvalue(v));
		break;
	case KTAP_TSTR:
		kp_puts(ks, svalue(v));
		break;
	case KTAP_TTAB:
		kp_printf(ks, "table 0x%lx", (unsigned long)hvalue(v));
		break;
	case KTAP_TEVENTSTR:
		/* check event context */
		if (!ks->current_event) {
			kp_error(ks,
			"cannot stringify event str in invalid context\n");
			return;
		}

		kp_transport_event_write(ks, ks->current_event);
		break;
	case KTAP_TKSTACK:
		kp_transport_print_kstack(ks, v->val.stack.depth,
					      v->val.stack.skip);
		break;
        default:
		kp_error(ks, "print unknown value type: %d\n", itype(v));
		break;
	}
}


/*
 * equality of ktap values.
 */
int kp_obj_rawequal(const ktap_val_t *t1, const ktap_val_t *t2)
{
	switch (itype(t1)) {
	case KTAP_TNIL:
	case KTAP_TTRUE:
	case KTAP_TFALSE:
		return 1;
	case KTAP_TNUM:
		return nvalue(t1) == nvalue(t2);
	case KTAP_TLIGHTUD:
		return pvalue(t1) == pvalue(t2);
	case KTAP_TFUNC:
		return fvalue(t1) == fvalue(t2);
	case KTAP_TSTR:
		return rawtsvalue(t1) == rawtsvalue(t2);
	case KTAP_TTAB:
		return hvalue(t1) == hvalue(t2);
	default:
		return gcvalue(t1) == gcvalue(t2);
	}

	return 0;
}

/*
 * ktap will not use lua's length operator for table,
 * also # is not for length operator any more in ktap.
 */
int kp_obj_len(ktap_state_t *ks, const ktap_val_t *v)
{
	switch(itype(v)) {
	case KTAP_TTAB:
		return kp_tab_len(ks, hvalue(v));
	case KTAP_TSTR:
		return rawtsvalue(v)->len;
	default:
		kp_printf(ks, "cannot get length of type %d\n", v->type);
		return -1;
	}
	return 0;
}

/* need to protect allgc field? */
ktap_obj_t *kp_obj_new(ktap_state_t *ks, size_t size)
{
	ktap_obj_t *o, **list;

	if (ks != G(ks)->mainthread) {
		kp_error(ks, "kp_obj_new only can be called in mainthread\n");
		return NULL;
	}

	o = kp_malloc(ks, size);
	if (unlikely(!o))
		return NULL;

	list = &G(ks)->allgc;
	gch(o)->nextgc = *list;
	*list = o;

	return o;
}


/* this function may be time consuming, move out from table set/get? */
ktap_str_t *kp_obj_kstack2str(ktap_state_t *ks, uint16_t depth, uint16_t skip)
{
	struct stack_trace trace;
	unsigned long *bt;
	char *btstr, *p;
	int i;

	bt = kp_this_cpu_print_buffer(ks); /* use print percpu buffer */
	trace.nr_entries = 0;
	trace.skip = skip;
	trace.max_entries = depth;
	trace.entries = (unsigned long *)(bt + 1);
	save_stack_trace(&trace);

	/* convert backtrace to string */
	p = btstr = kp_this_cpu_temp_buffer(ks);
	for (i = 0; i < trace.nr_entries; i++) {
		unsigned long addr = trace.entries[i];

		if (addr == ULONG_MAX)
			break;

		p += sprint_symbol(p, addr);
		*p++ = '\n';
        }

	return kp_str_new(ks, btstr, p - btstr);
}

static void free_gclist(ktap_state_t *ks, ktap_obj_t *o)
{
	while (o) {
		ktap_obj_t *next;

		next = gch(o)->nextgc;
		switch (gch(o)->gct) {
		case ~KTAP_TTAB:
			kp_tab_free(ks, (ktap_tab_t *)o);
			break;
		case ~KTAP_TUPVAL:
			kp_freeupval(ks, (ktap_upval_t *)o);
			break;
		default:
			kp_free(ks, o);
		}
		o = next;
	}
}

void kp_obj_freeall(ktap_state_t *ks)
{
	free_gclist(ks, G(ks)->allgc);
	G(ks)->allgc = NULL;
}

