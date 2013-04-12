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

#include <linux/ftrace_event.h>
#include "../ktap.h"

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

		s->buffer[len] = '\0';
		setsvalue(ra, tstring_assemble(ks, s->buffer, len + 1));
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

void ktap_call_probe_closure(ktap_State *mainthread, Closure *cl,
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

static void *entry_percpu_buffer;
DEFINE_PER_CPU(bool, ktap_in_tracing);

struct ktap_event_file {
	struct ftrace_event_file file;
	ktap_State *ks;
	Closure *cl;
};

static void *ktap_events_pre_trace(struct ftrace_event_file *file,
				   int entry_size, void *data)
{
	struct trace_entry  *entry;
	struct ktap_event_file *ktap_file;
	struct trace_descriptor_t *desc = data;
	unsigned long irq_flags;

	if (unlikely(entry_size > PAGE_SIZE))
		return NULL;

	ktap_file = container_of(file, struct ktap_event_file, file);
	if (same_thread_group(current, G(ktap_file->ks)->task))
		return NULL;

	local_irq_save(irq_flags);

	if (unlikely(__this_cpu_read(ktap_in_tracing))) {
		local_irq_restore(irq_flags);
		return NULL;
	}

	__this_cpu_write(ktap_in_tracing, true);

	entry = per_cpu_ptr(entry_percpu_buffer, smp_processor_id());
	entry->type = file->event_call->event.type;

	desc->irq_flags = irq_flags;

	return entry;
}

/* core probe function called by tracepoint */
static void ktap_events_do_trace(struct ftrace_event_file *file, void *entry,
				 int entry_size, void *data)
{
	struct trace_descriptor_t *desc = data;
	struct ktap_event_file *ktap_file;
	struct ktap_event event;

	ktap_file = container_of(file, struct ktap_event_file, file);

	event.call = ktap_file->file.event_call;
	event.entry = entry;
	event.entry_size = entry_size;

	ktap_call_probe_closure(ktap_file->ks, ktap_file->cl, &event);

	__this_cpu_write(ktap_in_tracing, false);
	local_irq_restore(desc->irq_flags);
}

static struct event_trace_ops ktap_events_ops = {
	.pre_trace = ktap_events_pre_trace,
	.do_trace  = ktap_events_do_trace,
};

struct trace_array ktap_tr = {
	.events = LIST_HEAD_INIT(ktap_tr.events),
	.ops = &ktap_events_ops,
};

/* helper function for ktap register tracepoint */
void ftrace_on_event_call(const char *buf, ftrace_call_func actor, void *data)
{
	char *event = NULL, *sub = NULL, *match, *buf_ptr = NULL;
	char new_buf[32] = {0};
	struct ftrace_event_call *call;

	if (buf) {
		/* argument buf is const, so we need to prepare a changeable buff */
		strncpy(new_buf, buf, 31);
		buf_ptr = new_buf;
	}

	/*
	 * The buf format can be <subsystem>:<event-name>
	 *  *:<event-name> means any event by that name.
	 *  :<event-name> is the same.
	 *
	 *  <subsystem>:* means all events in that subsystem
	 *  <subsystem>: means the same.
	 *
	 *  <name> (no ':') means all events in a subsystem with
	 *  the name <name> or any event that matches <name>
	 */

	match = strsep(&buf_ptr, ":");
	if (buf_ptr) {
		sub = match;
		event = buf_ptr;
		match = NULL;

		if (!strlen(sub) || strcmp(sub, "*") == 0)
			sub = NULL;
		if (!strlen(event) || strcmp(event, "*") == 0)
			event = NULL;
	}

	list_for_each_entry(call, &ftrace_events, list) {

		if (!call->name || !call->class || !call->class->reg)
			continue;

		if (call->flags & TRACE_EVENT_FL_IGNORE_ENABLE)
			continue;

		if (match &&
		    strcmp(match, call->name) != 0 &&
		    strcmp(match, call->class->system) != 0)
			continue;

		if (sub && strcmp(sub, call->class->system) != 0)
			continue;

		if (event && strcmp(event, call->name) != 0)
			continue;

		(*actor)(call, data);
	}
}

static void enable_event(struct ftrace_event_call *call, void *data)
{
	struct ktap_trace_arg *arg = data;
	struct ktap_event_file *ktap_file;

	ktap_printf(arg->ks, "enable tracepoint event: %s\n", call->name);

	ktap_file = ktap_malloc(arg->ks, sizeof(*ktap_file));
	if (!ktap_file) {
		ktap_printf(arg->ks, "allocate ktap_event_file failed\n");
		return;
	}
	ktap_file->file.event_call = call;
	ktap_file->file.tr = &ktap_tr;
	ktap_file->ks = arg->ks;
	ktap_file->cl = arg->cl;

	list_add_rcu(&ktap_file->file.list, &ktap_tr.events);

	call->class->reg(call, TRACE_REG_REGISTER, &ktap_file->file);
}

int start_trace(ktap_State *ks, const char *event_name, Closure *cl)
{
	struct ktap_trace_arg arg;

	if (*event_name == '\0')
		event_name = NULL;

	arg.ks = ks;
	arg.cl = cl;
	ftrace_on_event_call(event_name, enable_event, (void *)&arg);
	return 0;
}

/* cleanup all tracepoint owned by this ktap */
void end_all_trace(ktap_State *ks)
{
	struct ftrace_event_file *file, *tmp;

	list_for_each_entry_rcu(file, &ktap_tr.events, list) {
		struct ftrace_event_call *call = file->event_call;

		call->class->reg(call, TRACE_REG_UNREGISTER, file);
	}

	tracepoint_synchronize_unregister();

	/* free after tracepoint_synchronize_unregister */
	list_for_each_entry_safe(file, tmp, &ktap_tr.events, list) {
		struct ktap_event_file *ktap_file =
			container_of(file, struct ktap_event_file, file);

		list_del(&ktap_file->file.list);
		ktap_free(ktap_file->ks, ktap_file);
	}
}

void ktap_trace_exit(ktap_State *ks)
{
	end_all_trace(ks);

	if (!G(ks)->trace_enabled)
		return;

	free_percpu(entry_percpu_buffer);
	free_percpu(percpu_trace_iterator);

	G(ks)->trace_enabled = 0;
}

int ktap_trace_init(ktap_State *ks)
{
	if (!G(ks)->trace_enabled) {
		entry_percpu_buffer = __alloc_percpu(PAGE_SIZE,
						     __alignof__(char));
		if (!entry_percpu_buffer)
			return -1;

		percpu_trace_iterator = alloc_percpu(struct trace_iterator);
		if (!percpu_trace_iterator) {
			free_percpu(entry_percpu_buffer);
			return -1;
		}

		G(ks)->trace_enabled = 1;
	}

	/* change it in future, ktap cannot use ktap_tr.events global variable */
	INIT_LIST_HEAD(&ktap_tr.events);
	return 0;
}


