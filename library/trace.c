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

#include "../../trace.h"
#include "../ktap.h"

typedef struct ktap_Callback_data {
	ktap_State *ks;
	Closure *cl;
} ktap_Callback_data;

struct ktap_event_node {
	struct ftrace_event_call *call;
	ktap_State *ks;
	Closure *cl;
	struct list_head list;
	struct hlist_node node;
};

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

static struct trace_iterator *percpu_trace_iterator;
/* e.annotate */
static void event_annotate(ktap_State *ks, struct ktap_event *event, StkId ra)
{
	struct trace_iterator *iter;
	struct trace_event *ev;
	enum print_line_t ret = TRACE_TYPE_NO_CONSUME;

	/* Simulate the iterator */

	/* iter can be a bit big for the stack, use percpu*/
	iter = per_cpu_ptr(percpu_trace_iterator, smp_processor_id());

	trace_seq_init(&iter->seq);
	iter->ent = event->entry;

	ev = &(event->call->event);
	if (ev)
		ret = ev->funcs->trace(iter, 0, ev);

	if (ret != TRACE_TYPE_NO_CONSUME) {
		struct trace_seq *s = &iter->seq;
		int len = s->len >= PAGE_SIZE ? PAGE_SIZE - 1 : s->len;

		setsvalue(ra, tstring_new_local(ks, s->buffer, len));
	} else
		setnilvalue(ra);
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
	struct ktap_event_node *eventnode;
	struct hlist_node *pos;
	struct ktap_event event;

	event.call = call;
	event.entry = entry;
	event.entry_size = entry_size;
	event.data_size = data_size;

	hlist_for_each_entry_rcu(eventnode, &call->ktap_callback_list, node) {
		ktap_State *ks = eventnode->ks;
		Closure *cl = eventnode->cl;

		if (same_thread_group(current, G(ks)->task))
			continue;

		call_user_closure(ks, cl, &event);
	}
}


static void *entry_percpu_buffer;
static DEFINE_PER_CPU(bool, ktap_in_tracing);

void *ktap_pre_trace(struct ftrace_event_call *call, int size, unsigned long *flags)
{
	struct trace_entry  *entry;

	if (unlikely(size > PAGE_SIZE))
		return NULL;

	local_irq_save(*flags);

	if (unlikely(__this_cpu_read(ktap_in_tracing))) {
		local_irq_restore(*flags);
		return NULL;
	}

	__this_cpu_write(ktap_in_tracing, true);

	entry = per_cpu_ptr(entry_percpu_buffer, smp_processor_id());
	entry->type = call->event.type;

	return entry;
}

void ktap_post_trace(struct ftrace_event_call *call, void *entry, unsigned long *flags)
{
	__this_cpu_write(ktap_in_tracing, false);
	local_irq_restore(*flags);
}

static void enable_event(struct ftrace_event_call *call, void *data)
{
	ktap_Callback_data *cbdata = data;
	struct ktap_event_node *eventnode;

	ktap_printf(cbdata->ks, "enable event: %s\n", call->name);

	eventnode = ktap_malloc(cbdata->ks, sizeof(struct ktap_event_node));
	if (!eventnode) {
		ktap_printf(cbdata->ks, "allocate ktap_event_node failed\n");
		return;
	}
	eventnode->call = call;
	eventnode->ks = cbdata->ks;
	eventnode->cl = cbdata->cl;
	INIT_LIST_HEAD(&eventnode->list);

	if (!call->ktap_refcount) {
		call->ktap_pre_trace = ktap_pre_trace;
		call->ktap_do_trace = ktap_do_trace;
		call->ktap_post_trace = ktap_post_trace;
		INIT_HLIST_HEAD(&call->ktap_callback_list);

		if (!call->class->ktap_probe) {
			/* syscall tracing */
			if (start_trace_syscalls(call)) {
				ktap_free(cbdata->ks, eventnode);
				return;
			}
		} else
			tracepoint_probe_register(call->name,
						  call->class->ktap_probe,
						  call);
	}

	list_add(&eventnode->list, &(G(cbdata->ks)->event_nodes));
	hlist_add_head_rcu(&eventnode->node, &call->ktap_callback_list);

	call->ktap_refcount++;
}

int start_trace(ktap_State *ks, char *event_name, Closure *cl)
{
	ktap_Callback_data  callback;

	if (*event_name == '\0')
		event_name = NULL;

	callback.ks = ks;
	callback.cl = cl;
	ftrace_on_event_call(event_name, enable_event, (void *)&callback);
	return 0;
}

/* cleanup all tracepoint owned by this ktap */
void end_all_trace(ktap_State *ks)
{
	struct ktap_event_node *pos, *tmp;
	struct list_head *event_nodes = &(G(ks)->event_nodes);

	if (list_empty(event_nodes))
		return;

	list_for_each_entry_safe(pos, tmp, event_nodes, list) {
		struct ftrace_event_call *call = pos->call;

		hlist_del_rcu(&pos->node);

		if (!--call->ktap_refcount) {
			if (!call->class->ktap_probe)
				stop_trace_syscalls(call);
			else
				tracepoint_probe_unregister(call->name,
							    call->class->ktap_probe,
							    call);

			call->ktap_do_trace = NULL;
		}

		ktap_free(ks, pos);
	}

	tracepoint_synchronize_unregister();

	free_percpu(entry_percpu_buffer);
	free_percpu(percpu_trace_iterator);

	INIT_LIST_HEAD(event_nodes);
}

int ktap_trace_init()
{
	entry_percpu_buffer = alloc_percpu(PAGE_SIZE);
	if (!entry_percpu_buffer)
		return -1;

	percpu_trace_iterator = alloc_percpu(struct trace_iterator);
	if (!percpu_trace_iterator) {
		free_percpu(entry_percpu_buffer);
		return -1;
	}

	return 0;
}


