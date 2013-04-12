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
	return 0;
}

