/*
 * probe.c - ktap probing core implementation
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

#include <linux/perf_event.h>
#include <linux/ftrace_event.h>
#include <linux/kprobes.h>
#include "../ktap.h"

DEFINE_PER_CPU(bool, ktap_in_tracing);

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


struct ktap_kprobe {
	struct list_head list;
	struct kprobe p;
	ktap_State *ks;
	Closure *cl;
};

/* kprobe handler is called in interrupt disabled? */
static int __kprobes pre_handler_kprobe(struct kprobe *p, struct pt_regs *regs)
{
	struct ktap_kprobe *kp;
	ktap_State *ks;

	if (unlikely(__this_cpu_read(ktap_in_tracing)))
		return 0;

	__this_cpu_write(ktap_in_tracing, true);

	kp = container_of(p, struct ktap_kprobe, p);

	if (same_thread_group(current, G(kp->ks)->task))
		goto out;

	ks = ktap_newthread(kp->ks);
	setcllvalue(ks->top, kp->cl);
	incr_top(ks);
	ktap_call(ks, ks->top - 1, 0);
	ktap_exitthread(ks);

 out:
	__this_cpu_write(ktap_in_tracing, false);
	return 0;
}

static int start_kprobe(ktap_State *ks, const char *event_name, Closure *cl)
{
	struct ktap_kprobe *kp;

	kp = ktap_zalloc(ks, sizeof(*kp));
	kp->ks = ks;
	kp->cl = cl;

	INIT_LIST_HEAD(&kp->list);
	list_add(&kp->list, &(G(ks)->kprobes));

	kp->p.symbol_name = event_name;
	kp->p.pre_handler = pre_handler_kprobe;
	kp->p.post_handler = NULL;
	kp->p.fault_handler = NULL;
	kp->p.break_handler = NULL;

	if (register_kprobe(&kp->p)) {
		ktap_printf(ks, "Cannot register probe: %s\n", event_name);
		list_del(&kp->list);
		return -1;
	}

	return 0;
}

struct ktap_perf_event {
	struct perf_event *event;
	struct hlist_node hlist_entry;
	ktap_State *ks;
	Closure *cl;
};

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

/* e.print_fmt */
static void event_print_fmt(ktap_State *ks, struct ktap_event *event, StkId ra)
{
	setsvalue(ra, tstring_new(ks, event->call->print_fmt));
}

#if 0

/* e.narg */
static void event_narg(ktap_State *ks, struct ktap_event *event, StkId ra)
{
	setsvalue(ra, tstring_new(ks, event->call->name));
}

static struct list_head *ktap_get_fields(struct ftrace_event_call *event_call)
{
	if (!event_call->class->get_fields)
		return &event_call->class->fields;
	return event_call->class->get_fields(event_call);
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
#endif

#define EVENT_FIELD_BASE	100

static struct event_field_tbl {
	char *name;
	void (*func)(ktap_State *ks, struct ktap_event *event, StkId ra);	
} event_ftbl[] = {
	{"annotate", event_annotate},
	{"name", event_name},
	{"print_fmt", event_print_fmt},
#if 0
	{"allfield", event_allfield},
	{"field1", event_field1}
#endif
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

	if (index < EVENT_FIELD_BASE) {
		//event_field(ks, event, index, ra);
	} else
		event_ftbl[index - EVENT_FIELD_BASE].func(ks, event, ra);
}


struct hlist_head __percpu *perf_events_list;

/* Callback function for perf event subsystem */
static void ktap_overflow_callback(struct perf_event *event,
				   struct perf_sample_data *data,
				   struct pt_regs *regs)
{
	struct ktap_perf_event *ktap_pevent;
	struct hlist_head *head;
	struct ktap_event e;
	unsigned long irq_flags;

	e.call = event->tp_event;
	e.entry = data->raw->data;
	e.entry_size = data->raw->size;

	if (unlikely(__this_cpu_read(ktap_in_tracing)))
		return;

	local_irq_save(irq_flags);
	__this_cpu_write(ktap_in_tracing, true);

	head = this_cpu_ptr(perf_events_list);
	hlist_for_each_entry_rcu(ktap_pevent, head, hlist_entry) {
		ktap_State *ks = ktap_pevent->ks;

		if (same_thread_group(current, G(ks)->task))
			continue;

		ktap_call_probe_closure(ks, ktap_pevent->cl, &e);
        }

	__this_cpu_write(ktap_in_tracing, false);
	local_irq_restore(irq_flags);
}

struct list_head *ftrace_events_ptr;
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

	list_for_each_entry(call, ftrace_events_ptr, list) {

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


static void enable_tracepoint_on_cpu(int cpu, struct perf_event_attr *attr,
				     struct ftrace_event_call *call,
				     struct ktap_trace_arg *arg)
{
	struct ktap_perf_event *ktap_pevent;
	struct perf_event *event;

	event = perf_event_create_kernel_counter(attr, cpu, NULL,
						 ktap_overflow_callback, NULL);
	if (IS_ERR(event)) {
		int err = PTR_ERR(event);
		ktap_printf(arg->ks, "unable create tracepoint event %s on cpu %d, err: %d\n",
				call->name, cpu, err);
		return;
	}

	ktap_pevent = ktap_zalloc(arg->ks, sizeof(*ktap_pevent));
	ktap_pevent->event = event;
	ktap_pevent->ks = arg->ks;
	ktap_pevent->cl = arg->cl;
	hlist_add_head_rcu(&ktap_pevent->hlist_entry,
			   per_cpu_ptr(perf_events_list, cpu));

	perf_event_enable(event);
}

static void enable_tracepoint(struct ftrace_event_call *call, void *data)
{
	struct ktap_trace_arg *arg = data;
	struct perf_event_attr attr;
	int cpu;

	ktap_printf(arg->ks, "enable tracepoint event: %s\n", call->name);

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_TRACEPOINT;	
	attr.config = call->event.type;
	attr.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
			   PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD;
	attr.sample_period = 1;
	attr.size = sizeof(attr);

	for_each_possible_cpu(cpu)
		enable_tracepoint_on_cpu(cpu, &attr, call, arg);
}

static int start_tracepoint(ktap_State *ks, const char *event_name, Closure *cl)
{
	struct ktap_trace_arg arg;

	if (*event_name == '\0')
		event_name = NULL;

	arg.ks = ks;
	arg.cl = cl;
	ftrace_on_event_call(event_name, enable_tracepoint, (void *)&arg);
	return 0;
}

int start_probe(ktap_State *ks, const char *event_name, Closure *cl)
{
	if (!strncmp(event_name, "kprobe:", 7)) {
		return start_kprobe(ks, event_name + 7, cl);
	} else if (!strncmp(event_name, "kprobes:", 8)) {
		return start_kprobe(ks, event_name + 8, cl);
	} else if (!strncmp(event_name, "tracepoint:", 11)) {
		return start_tracepoint(ks, event_name + 11, cl);
	} else if (!strncmp(event_name, "tp:", 3)) {
		return start_tracepoint(ks, event_name + 3, cl);
	} else {
		ktap_printf(ks, "unknown probe event name: %s\n", event_name);
		return -1;
	}

}

void end_probes(struct ktap_State *ks)
{
	struct list_head *kprobes_list = &(G(ks)->kprobes);
	struct ktap_kprobe *kp, *tmp;
	int cpu;

	list_for_each_entry(kp, kprobes_list, list) {
		unregister_kprobe(&kp->p);
	}

	synchronize_sched();

	list_for_each_entry_safe(kp, tmp, kprobes_list, list) {
		list_del(&kp->list);
		ktap_free(ks, kp);
	}

	for_each_possible_cpu(cpu) {
		struct ktap_perf_event *ktap_pevent;
		struct hlist_head *head;
		struct hlist_node *htmp;

		head = per_cpu_ptr(perf_events_list, cpu);
		hlist_for_each_entry_rcu(ktap_pevent, head, hlist_entry) {
			perf_event_disable(ktap_pevent->event);
			perf_event_release_kernel(ktap_pevent->event);
        	}
        	/*
		 * Ensure our callback won't be called anymore. The buffers
		 * will be freed after that.
		 */
        	tracepoint_synchronize_unregister();

		hlist_for_each_entry_safe(ktap_pevent, htmp, head, hlist_entry) {
			hlist_del_rcu(&ktap_pevent->hlist_entry);
			ktap_free(ks, ktap_pevent);
		}
	}
}

void ktap_probe_exit(ktap_State *ks)
{
	end_probes(ks);
	free_percpu(perf_events_list);
	perf_events_list = NULL;

	if (!G(ks)->trace_enabled)
		return;

	free_percpu(percpu_trace_iterator);

	G(ks)->trace_enabled = 0;
}

int ktap_probe_init(ktap_State *ks)
{
	struct hlist_head __percpu *list;
	int cpu;

	list = alloc_percpu(struct hlist_head);
	if (!list)
		return -1;

	for_each_possible_cpu(cpu)
		INIT_HLIST_HEAD(per_cpu_ptr(list, cpu));

	perf_events_list = list;

	/* allocate percpu data */
	if (!G(ks)->trace_enabled) {
		percpu_trace_iterator = alloc_percpu(struct trace_iterator);
		if (!percpu_trace_iterator)
			return -1;

		G(ks)->trace_enabled = 1;
	}

	/* get ftrace_events global variable if ftrace_events not exported */
	ftrace_events_ptr = kallsyms_lookup_name("ftrace_events");
	if (!ftrace_events_ptr) {
		ktap_printf(ks, "cannot lookup ftrace_events in kallsyms\n");
		return -1;
	}

	return 0;
}
