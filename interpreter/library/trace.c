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
#include "../../include/ktap.h"

struct ktap_event_file {
	struct ftrace_event_file file;
	ktap_State *ks;
	Closure *cl;
};

static void *entry_percpu_buffer;
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
	free_percpu(entry_percpu_buffer);
}

int ktap_trace_init(ktap_State *ks)
{	
	entry_percpu_buffer = __alloc_percpu(PAGE_SIZE,
					     __alignof__(char));
	if (!entry_percpu_buffer)
		return -1;


	/* change it in future, ktap cannot use ktap_tr.events global variable */
	INIT_LIST_HEAD(&ktap_tr.events);
	return 0;
}


