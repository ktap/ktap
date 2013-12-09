/*
 * kdebug.c - ktap probing core implementation
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
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

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/version.h>
#include <linux/ftrace_event.h>
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_transport.h"
#include "kp_vm.h"

static void ktap_call_probe_closure(ktap_state *mainthread, ktap_closure *cl,
				    struct ktap_event *e)
{
	ktap_state *ks;
	ktap_value *func;

	ks = kp_newthread(mainthread);
	set_closure(ks->top, cl);
	func = ks->top;
	incr_top(ks);

	ks->current_event = e;

	kp_call(ks, func, 0);

	ks->current_event = NULL;
	kp_exitthread(ks);
}

void kp_event_tostring(ktap_state *ks, struct trace_seq *seq)
{
	struct ktap_event *e = ks->current_event;
	struct trace_iterator *iter;
	struct trace_event *ev;
	enum print_line_t ret = TRACE_TYPE_NO_CONSUME;

	/* Simulate the iterator */

	/*
	 * use temp percpu buffer as trace_iterator
	 * we cannot use same temp buffer as printf.
	 */
	iter = kp_percpu_data(ks, KTAP_PERCPU_DATA_BUFFER2);

	trace_seq_init(&iter->seq);
	iter->ent = e->entry;

	ev = &(e->call->event);
	if (ev)
		ret = ev->funcs->trace(iter, 0, ev);

	if (ret != TRACE_TYPE_NO_CONSUME) {
		struct trace_seq *s = &iter->seq;
		int len = s->len >= PAGE_SIZE ? PAGE_SIZE - 1 : s->len;

		s->buffer[len] = '\0';
		_trace_seq_puts(seq, s->buffer);
	}
}

/* This definition should keep update with kernel/trace/trace.h */
struct ftrace_event_field {
	struct list_head        link;
	const char              *name;
	const char              *type;
	int                     filter_type;
	int                     offset;
	int                     size;
	int                     is_signed;
};

static struct list_head *ktap_get_fields(struct ftrace_event_call *event_call)
{
	if (!event_call->class->get_fields)
		return &event_call->class->fields;
	return event_call->class->get_fields(event_call);
}

static void get_field_value(ktap_state *ks, struct ktap_event *e,
			    struct ftrace_event_field *field, ktap_value *ra)
{
	void *value = (unsigned char *)e->entry + field->offset;

	if (field->size == 4) {
		int n = *(int *)value;
		set_number(ra, n);
		return;
	} else if (field->size == 8) {
		long n = *(long *)value;
		set_number(ra, n);
		return;
	}

	if (!strncmp(field->type, "char", 4)) {
		set_string(ra, kp_tstring_new(ks, (char *)value));
		return;
	}
}

void kp_event_getarg(ktap_state *ks, ktap_value *ra, int n)
{
	struct ktap_event *e = ks->current_event;
	int index = n;
	struct ftrace_event_field *field;
	struct list_head *head;

	/* this is very slow and not safe, fix it in future */
	head = ktap_get_fields(e->call);
	list_for_each_entry_reverse(field, head, link) {
		if (--index == 0) {
			get_field_value(ks, e, field, ra);
			return;
		}
	}

	set_nil(ra);
	return;
}

/* Callback function for perf event subsystem
 * make ktap reentrant, don't disable irq in callback function,
 * same as perf and ftrace. to make reentrant, we need some
 * percpu data to be context isolation(irq/sirq/nmi/process)
 *
 * The recursion checking in here is mainly purpose for avoiding
 * corrupt ktap_state with timer closure callback. For tracepoint
 * recusion, perf core already handle it.
 *
 * Note tracepoint handler is calling with rcu_read_lock.
 */
static void ktap_overflow_callback(struct perf_event *event,
				   struct perf_sample_data *data,
				   struct pt_regs *regs)
{
	struct ktap_probe_event *ktap_pevent;
	struct ktap_event e;
	ktap_state  *ks;
	int rctx;

	ktap_pevent = event->overflow_handler_context;
	ks = ktap_pevent->ks;

	if (unlikely(ks->stop))
		return;

	rctx = get_recursion_context(ks);
	if (rctx < 0)
		return;

	KTAP_STATS(ks)->events_hits += 1;

	/* profile perf event don't have valid associated tp_event */
	if (event->tp_event) {
		e.call = event->tp_event;
		e.entry = data->raw->data;
		e.entry_size = data->raw->size;
	}
	e.pevent = ktap_pevent;
	e.regs = regs;

	ktap_call_probe_closure(ks, ktap_pevent->cl, &e);

	put_recursion_context(ks, rctx);
}

static void perf_destructor(struct ktap_probe_event *ktap_pevent)
{
	perf_event_release_kernel(ktap_pevent->perf);
}

static int (*kp_ftrace_profile_set_filter)(struct perf_event *event,
					   int event_id, char *filter_str);

/*
 * Generic perf event register function
 * used by tracepoints/kprobe/uprobe/profile-timer/hw_breakpoint.
 */
void kp_perf_event_register(ktap_state *ks, struct perf_event_attr *attr,
			    struct task_struct *task, char *filter,
			    ktap_closure *cl)
{
	struct ktap_probe_event *ktap_pevent;
	struct perf_event *event;
	int cpu, ret;

	kp_verbose_printf(ks, "enable perf event id: %d, filter: %s "
			      "pid: %d\n", attr->config, filter,
			      task ? task_tgid_vnr(task) : -1);

	/*
	 * don't tracing until ktap_wait, the reason is:
	 * 1). some event may hit before apply filter
	 * 2). more simple to manage tracing thread
	 * 3). avoid race with mainthread.
	 *
	 * Another way to do this is make attr.disabled as 1, then use
	 * perf_event_enable after filter apply, however, perf_event_enable
	 * was not exported in kernel older than 3.3, so we drop this method.
	 */
	ks->stop = 1;

	for_each_cpu(cpu, G(ks)->cpumask) {
		ktap_pevent = kp_zalloc(ks, sizeof(*ktap_pevent));
		ktap_pevent->ks = ks;
		ktap_pevent->cl = cl;
		event = perf_event_create_kernel_counter(attr, cpu, task,
							 ktap_overflow_callback,
							 ktap_pevent);
		if (IS_ERR(event)) {
			int err = PTR_ERR(event);
			kp_error(ks, "unable register perf event %d on cpu %d, "
				     "err: %d\n", attr->config, cpu, err);
			kp_free(ks, ktap_pevent);
			return;
		}

		ktap_pevent->perf = event;
		INIT_LIST_HEAD(&ktap_pevent->list);
		list_add_tail(&ktap_pevent->list, &G(ks)->probe_events_head);

		if (!filter)
			continue;

		ret = kp_ftrace_profile_set_filter(event, attr->config, filter);
		if (ret) {
			kp_error(ks, "unable set filter %s for event id %d, "
				     "ret: %d\n", filter, attr->config, ret);
			perf_destructor(ktap_pevent);
			list_del(&ktap_pevent->list);
			kp_free(ks, ktap_pevent);
			return;
		}
	}
}

static void end_probes(struct ktap_state *ks)
{
	struct ktap_probe_event *ktap_pevent;
	struct list_head *tmp, *pos;
	struct list_head *head = &G(ks)->probe_events_head;

	list_for_each(pos, head) {
		ktap_pevent = container_of(pos, struct ktap_probe_event,
					   list);
		perf_destructor(ktap_pevent);
        }
       	/*
	 * Ensure our callback won't be called anymore. The buffers
	 * will be freed after that.
	 */
	tracepoint_synchronize_unregister();

	list_for_each_safe(pos, tmp, head) {
		ktap_pevent = container_of(pos, struct ktap_probe_event,
					   list);
		list_del(&ktap_pevent->list);
		kp_free(ks, ktap_pevent);
	}
}

static int ktap_lib_probe_by_id(ktap_state *ks)
{
	ktap_closure *cl;
	struct task_struct *task = G(ks)->trace_task;
	ktap_eventdef_info evdef_info;
	char *filter = NULL;
	int *id_arr;
	int ret, i;

	/* the number is userspace address refer to ktap_eventdef_info */
	kp_arg_check(ks, 1, KTAP_TNUMBER);
	kp_arg_check(ks, 2, KTAP_TFUNCTION);

	ret = copy_from_user(&evdef_info, (void *)nvalue(kp_arg(ks, 1)),
			     sizeof(evdef_info));
	if (ret < 0)
		return -1;

	if (evdef_info.filter) {
		int len;

		len = strlen_user(evdef_info.filter);
		if (len > 0x1000)
			return -1;

		filter = kmalloc(len + 1, GFP_KERNEL);
		if (!filter)
			return -1;

		if (strncpy_from_user(filter, evdef_info.filter, len) < 0) {
			kfree(filter);
			return -1;
		}
	}

	id_arr = kmalloc(evdef_info.nr * sizeof(int), GFP_KERNEL);
	if (!id_arr) {
		kfree(filter);
		return -1;
	}

	ret = copy_from_user(id_arr, evdef_info.id_arr,
			     evdef_info.nr * sizeof(int));
	if (ret < 0) {
		kfree(filter);
		kfree(id_arr);
		return -1;
	}

	cl = clvalue(kp_arg(ks, 2));

	for (i = 0; i < evdef_info.nr; i++) {
		struct perf_event_attr attr;

		memset(&attr, 0, sizeof(attr));
		attr.type = PERF_TYPE_TRACEPOINT;	
		attr.config = id_arr[i];
		attr.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
				   PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD;
		attr.sample_period = 1;
		attr.size = sizeof(attr);
		attr.disabled = 0;

		kp_perf_event_register(ks, &attr, task, filter, cl);
	}

	kfree(filter);
	kfree(id_arr);
	return 0;
}

static int ktap_lib_probe_end(ktap_state *ks)
{
	kp_arg_check(ks, 1, KTAP_TFUNCTION);

	G(ks)->trace_end_closure = clvalue(kp_arg(ks, 1));
	return 0;
}

static int ktap_lib_traceoff(ktap_state *ks)
{
	end_probes(ks);

	/* call trace_end_closure after probed end */
	if (G(ks)->trace_end_closure) {
		set_closure(ks->top, G(ks)->trace_end_closure);
		incr_top(ks);
		kp_call(ks, ks->top - 1, 0);
		G(ks)->trace_end_closure = NULL;
	}

	return 0;
}

void kp_probe_exit(ktap_state *ks)
{
	if (!G(ks)->trace_enabled)
		return;

	end_probes(ks);

	/* call trace_end_closure after probed end */
	if (!G(ks)->error && G(ks)->trace_end_closure) {
		set_closure(ks->top, G(ks)->trace_end_closure);
		incr_top(ks);
		kp_call(ks, ks->top - 1, 0);
		G(ks)->trace_end_closure = NULL;
	}

	G(ks)->trace_enabled = 0;
}

int kp_probe_init(ktap_state *ks)
{
	G(ks)->trace_enabled = 1;
	return 0;
}

static const ktap_Reg kdebuglib_funcs[] = {
	{"probe_by_id", ktap_lib_probe_by_id},
	{"probe_end", ktap_lib_probe_end},
	{"traceoff", ktap_lib_traceoff},
	{NULL}
};

void kp_init_kdebuglib(ktap_state *ks)
{
	kp_ftrace_profile_set_filter =
		(void *)kallsyms_lookup_name("ftrace_profile_set_filter");
	if (!kp_ftrace_profile_set_filter) {
		kp_error(ks, "ktap: cannot lookup ftrace_profile_set_filter "
				"in kallsyms\n");
		return;
	}

	kp_register_lib(ks, "kdebug", kdebuglib_funcs);
}

