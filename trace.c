/*
 * trace.c - ktap tracing core implementation
 *
 * Copyright (C) 2012 Jovi Zhang <bookjovi@gmail.com>
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
#include <linux/kallsyms.h>
#include <linux/ftrace_event.h>

#include "../trace.h"
#include "ktap.h"

struct ktap_trace_list {
	struct ftrace_event_call *call;
	ktap_Callback_data *cbdata;
	struct list_head list;
};

/* todo: put this list to global ktap State and percpu? */
static LIST_HEAD(trace_list);


/* this structure allocate on stack */
struct ktap_event {
	struct ftrace_event_call *call;
	void *entry;
	int entry_size;
	int data_size;
};


static struct list_head *ktap_get_fields(struct ftrace_event_call *event_call)
{
	if (!event_call->class->get_fields)
		return &event_call->class->fields;
	return event_call->class->get_fields(event_call);
}

/* e.annotate */
static void event_annotate(ktap_State *ks, struct ktap_event *event, StkId ra)
{
	struct trace_iterator *iter;
	struct trace_event *ev;
	enum print_line_t ret = TRACE_TYPE_NO_CONSUME;

	/* Simulate the iterator */

	/* iter can be a bit big for the stack */
	iter = ktap_malloc(ks, sizeof(*iter));

	trace_seq_init(&iter->seq);
	iter->ent = event->entry;

	ev = &(event->call->event);
	if (ev)
		ret = ev->funcs->trace(iter, 0, ev);

	if (ret != TRACE_TYPE_NO_CONSUME) {
		struct trace_seq *s = &iter->seq;
		int len = s->len >= PAGE_SIZE ? PAGE_SIZE - 1 : s->len;

		setsvalue(ra, tstring_newlstr(ks, s->buffer, len));
	} else
		setnilvalue(ra);

	ktap_free(ks, iter);
}

/* e.name */
static void event_name(ktap_State *ks, struct ktap_event *event, StkId ra)
{
	setsvalue(ra, tstring_new(ks, event->call->name));
}

/* e.narg */
static void event_narg(ktap_State *ks, struct ktap_event *event, StkId ra)
{
	setsvalue(ra, tstring_new(ks, event->call->name));
}

/* e.print_fmt */
static void event_print_fmt(ktap_State *ks, struct ktap_event *event, StkId ra)
{
	setsvalue(ra, tstring_new(ks, event->call->print_fmt));
}

/* e.allfield */
static void event_allfield(ktap_State *ks, struct ktap_event *event, StkId ra)
{
	char s[128];
	int len, pos = 0;
	struct ftrace_event_field *field;
	struct list_head *head;

	head = ktap_get_fields(event->call);
	list_for_each_entry_reverse(field, head, link) {
		len = sprintf(s + pos, "[%s-%s-%d-%d-%d] ", field->name, field->type,
				 field->offset, field->size, field->is_signed);
		pos += len;
	}
	s[pos] = '\0';

	setsvalue(ra, tstring_new(ks, s));
}

static void event_field(ktap_State *ks, struct ktap_event *event, int index, StkId ra)
{
	struct ftrace_event_field *field;
	struct list_head *head;

	head = ktap_get_fields(event->call);
	list_for_each_entry_reverse(field, head, link) {
		if ((--index == 0) && (field->size == 4)) {
			int n = *(int *)((unsigned char *)event->entry + field->offset);
			setnvalue(ra, n);
			return;
		}
	}

	setnilvalue(ra);
}


static void event_field1(ktap_State *ks, struct ktap_event *event, StkId ra)
{
	event_field(ks, event, 1, ra);
}

#define EVENT_FIELD_BASE	100

static struct event_field_tbl {
	char *name;
	void (*func)(ktap_State *ks, struct ktap_event *event, StkId ra);	
} event_ftbl[] = {
	{"annotate", event_annotate},
	{"name", event_name},
	{"print_fmt", event_print_fmt},
	{"allfield", event_allfield},
	{"field1", event_field1}
};

int ktap_event_get_index(const char *field)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(event_ftbl); i++) {
		if (!strcmp(event_ftbl[i].name, field)) {
			return EVENT_FIELD_BASE + i;
		}
	}

	return -1;
}

Tstring *ktap_event_get_ts(ktap_State *ks, int index)
{
	return tstring_new(ks, event_ftbl[index - EVENT_FIELD_BASE].name);
}


void ktap_event_handle(ktap_State *ks, void *e, int index, StkId ra)
{
	struct ktap_event *event = e;

	if (index < EVENT_FIELD_BASE)
		event_field(ks, event, index, ra);
	else
		event_ftbl[index - EVENT_FIELD_BASE].func(ks, event, ra);
}

static void call_user_closure(ktap_State *mainthread, Closure *cl,
			      struct ktap_event *event)
{
	ktap_State *ks;
	Tvalue *func;

	ks = ktap_newthread(mainthread);
	setcllvalue(ks->top, cl);
	func = ks->top;
	incr_top(ks);

	if (cl->l.p->numparams) {
		setevalue(ks->top, event);
		incr_top(ks);
	}

	ktap_call(ks, func, 0);
	ktap_exitthread(ks);
}

/* core probe function called by tracepoint */
void ktap_do_trace(struct ftrace_event_call *call, void *entry,
			  int entry_size, int data_size)
{
	ktap_Callback_data *cbdata;
	struct hlist_node *pos;
	struct ktap_event event;

	/* todo: fix this */
#if 0
	if (in_interrupt())
		return;
#endif
	event.call = call;
	event.entry = entry;
	event.entry_size = entry_size;
	event.data_size = data_size;

	hlist_for_each_entry_rcu(cbdata, pos,
				 &call->ktap_callback_list, node) {
		ktap_State *ks = cbdata->ks;
		Closure *cl = cbdata->cl;

		//call_user_closure(ks, cl, entry, trace_get_fields(call));
		call_user_closure(ks, cl, &event);
	}
}


static DEFINE_PER_CPU(bool, ktap_in_tracing);

static void *ktap_pre_trace(struct ftrace_event_call *call, int size)
{
	struct trace_entry  *entry;

	if (unlikely(__this_cpu_read(ktap_in_tracing)))
		return NULL;

	__this_cpu_write(ktap_in_tracing, true);

	entry = ktap_malloc(NULL, size);
	entry->type = call->event.type;

	return entry;
}

static void ktap_post_trace(struct ftrace_event_call *call, void *entry)
{
	ktap_free(NULL, entry);

	__this_cpu_write(ktap_in_tracing, false);
}

static void enable_event(struct ftrace_event_call *call, void *data)
{
	ktap_Callback_data *cbdata = data;
	
	ktap_printf(cbdata->ks, "enable event: %s\n", call->name);

	if (!call->ktap_refcount) {
		struct ktap_trace_list *ktl;

		ktl = ktap_malloc(cbdata->ks, sizeof(struct ktap_trace_list));
		if (!ktl) {
			ktap_printf(cbdata->ks, "allocate ktap_trace_list failed\n");
			return;
		}
		ktl->call = call;
		ktl->cbdata = data;
		INIT_LIST_HEAD(&ktl->list);

		call->ktap_pre_trace = ktap_pre_trace;
		call->ktap_do_trace = ktap_do_trace;
		call->ktap_post_trace = ktap_post_trace;

		if (!call->class->ktap_probe) {
			/* syscall tracing */
			if (start_trace_syscalls(call, cbdata)) {
				ktap_free(cbdata->ks, ktl);
				return;
			}
		} else
			tracepoint_probe_register(call->name,
						  call->class->ktap_probe,
						  call);
		list_add(&ktl->list, &trace_list);
	}

	hlist_add_head_rcu(&cbdata->node, &call->ktap_callback_list);
	call->ktap_refcount++;
	cbdata->event_refcount++;
}

int start_trace(ktap_State *ks, char *event_name, Closure *cl)
{
	ktap_Callback_data *callback;

	callback = kzalloc(sizeof(ktap_Callback_data), GFP_KERNEL);
	callback->ks = ks;
	callback->cl = cl;

	ftrace_on_event_call(event_name, enable_event, (void *)callback);
	return 0;
}

/* cleanup all tracepoint owned by this ktap */
void end_all_trace(ktap_State *ks)
{
	struct ktap_trace_list *pos;

	if (list_empty(&trace_list))
		return;

	list_for_each_entry(pos, &trace_list, list) {
		struct ftrace_event_call *call = pos->call;
		ktap_Callback_data *cbdata = pos->cbdata;

		hlist_del_rcu(&cbdata->node);

		if (!--call->ktap_refcount) {
			if (!call->class->ktap_probe)
				stop_trace_syscalls(call, cbdata);
			else
				tracepoint_probe_unregister(call->name,
							    call->class->ktap_probe,
							    call);

			call->ktap_do_trace = NULL;
			hlist_empty(&call->ktap_callback_list);
		}

		if (!--cbdata->event_refcount)
			ktap_free(ks, cbdata);
		ktap_free(ks, pos);
	}

	tracepoint_synchronize_unregister();

	/* empty trace_list */	
	INIT_LIST_HEAD(&trace_list);
}

