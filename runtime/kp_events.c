/*
 * kp_events.c - ktap events management (registry, destroy, event callback)
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
#include <linux/slab.h>
#include <linux/version.h>
#include <asm/syscall.h>
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_transport.h"
#include "kp_vm.h"
#include "kp_events.h"

const char *kp_event_tostr(ktap_state_t *ks)
{
	struct ktap_event_data *e = ks->current_event;
	struct ftrace_event_call *call;
	struct trace_iterator *iter;
	struct trace_event *ev;
	enum print_line_t ret = TRACE_TYPE_NO_CONSUME;
	static const char *dummy_msg = "argstr_not_available";

	/* need to check current context is vaild tracing context */
	if (!ks->current_event) {
		kp_error(ks, "cannot stringify event str in invalid context\n");
		return NULL;
	}

	/*check if stringified before */
	if (ks->current_event->argstr)
		return getstr(ks->current_event->argstr);

	/* timer event and raw tracepoint don't have associated argstr */
	if (e->event->type == KTAP_EVENT_TYPE_PERF && e->event->perf->tp_event)
		call = e->event->perf->tp_event;
	else
		return dummy_msg;

	/* Simulate the iterator */

	/*
	 * use temp percpu buffer as trace_iterator
	 * we cannot use same print_buffer because we may called from printf.
	 */
	iter = kp_this_cpu_temp_buffer(ks);

	trace_seq_init(&iter->seq);
	iter->ent = e->data->raw->data;

	ev = &(call->event);
	if (ev)
		ret = ev->funcs->trace(iter, 0, ev);

	if (ret != TRACE_TYPE_NO_CONSUME) {
		struct trace_seq *s = &iter->seq;
		int len = s->len >= PAGE_SIZE ? PAGE_SIZE - 1 : s->len;

		s->buffer[len] = '\0';
		return &s->buffer[0];
	}

	return dummy_msg;
}

/* return string repr of 'argstr' */
const ktap_str_t *kp_event_stringify(ktap_state_t *ks)
{
	const char *str;
	ktap_str_t *ts;

	/*check if stringified before */
	if (ks->current_event->argstr)
		return ks->current_event->argstr;

	str = kp_event_tostr(ks);
	if (!str)
		return NULL;

	ts = kp_str_newz(ks, str);
	ks->current_event->argstr = ts;
	return ts;
}

/*
 * This definition should keep update with kernel/trace/trace.h
 * TODO: export this struct in kernel 
 */
struct ftrace_event_field {
	struct list_head        link;
	const char              *name;
	const char              *type;
	int                     filter_type;
	int                     offset;
	int                     size;
	int                     is_signed;
};

static struct list_head *get_fields(struct ftrace_event_call *event_call)
{
	if (!event_call->class->get_fields)
		return &event_call->class->fields;
	return event_call->class->get_fields(event_call);
}

void kp_event_getarg(ktap_state_t *ks, ktap_val_t *ra, int idx)
{
	struct ktap_event_data *e = ks->current_event;
	struct ktap_event *event = e->event;
	struct ktap_event_field *event_fields = &event->fields[idx];

	switch (event_fields->type)  {
	case KTAP_EVENT_FIELD_TYPE_INT: {
		struct trace_entry *entry = e->data->raw->data;
		void *value = (unsigned char *)entry + event_fields->offset;
		int n = *(int *)value;
		set_number(ra, n);
		return;
		}
	case KTAP_EVENT_FIELD_TYPE_LONG: {
		struct trace_entry *entry = e->data->raw->data;
		void *value = (unsigned char *)entry + event_fields->offset;
		long n = *(long *)value;
		set_number(ra, n);
		return;
		}
	case KTAP_EVENT_FIELD_TYPE_STRING: {
		struct trace_entry *entry = e->data->raw->data;
		ktap_str_t *ts;
		void *value = (unsigned char *)entry + event_fields->offset;
		ts = kp_str_newz(ks, (char *)value);
		if (ts)
			set_string(ra, ts);
		else
			set_nil(ra);
		return;
		}
	case KTAP_EVENT_FIELD_TYPE_CONST: {
		set_number(ra, (ktap_number)event_fields->offset);
		return;
		}
	case KTAP_EVENT_FIELD_TYPE_REGESTER: {
		unsigned long *reg = (unsigned long *)((u8 *)e->regs +
					event_fields->offset);
		set_number(ra, *reg);
		return;
		}
	case KTAP_EVENT_FIELD_TYPE_NIL:
		set_nil(ra);
		return;
	case KTAP_EVENT_FIELD_TYPE_INVALID:
		kp_error(ks, "the field type is not supported yet\n");
		set_nil(ra);
		return;
	}
}

/* init all fields of event, for quick arg1..arg9 access */
static int init_event_fields(ktap_state_t *ks, struct ktap_event *event)
{
	struct ftrace_event_call *event_call = event->perf->tp_event; 
	struct ktap_event_field *event_fields = &event->fields[0];
	struct ftrace_event_field *field;
	struct list_head *head;
	int idx = 0, n = 0;

	/* only init fields for tracepoint, not timer event */
	if (!event_call)
		return 0;

	/* intern probename */
	event->name = kp_str_newz(ks, event_call->name);
	if (unlikely(!event->name))
		return -ENOMEM;

	head = get_fields(event_call);
	list_for_each_entry_reverse(field, head, link) {
		if (n++ == 9) {
			/*
			 * For some events have fields more than 9, just ignore
			 * those rest fields at present.
			 *
			 * TODO: support access all fields in tracepoint event
			 *
			 * Examples: mce:mce_record, ext4:ext4_writepages, ...
			 */
			return 0;
		}

		event_fields[idx].offset = field->offset;

		if (field->size == 4) {
			event_fields[idx].type = KTAP_EVENT_FIELD_TYPE_INT;
			idx++;
			continue;
		} else if (field->size == 8) {
			event_fields[idx].type = KTAP_EVENT_FIELD_TYPE_LONG;
			idx++;
			continue;
		}
		if (!strncmp(field->type, "char", 4)) {
			event_fields[idx].type = KTAP_EVENT_FIELD_TYPE_STRING;
			idx++;
			continue;
		}

		/* TODO: add more type check */
		event_fields[idx++].type = KTAP_EVENT_FIELD_TYPE_INVALID;
	}

	/* init all rest fields as NIL */
	while (idx < 9)
		event_fields[idx++].type = KTAP_EVENT_FIELD_TYPE_NIL;

	return 0;
}

static inline void call_probe_closure(ktap_state_t *mainthread,
				      ktap_func_t *fn,
				      struct ktap_event_data *e, int rctx)
{
	ktap_state_t *ks;
	ktap_val_t *func;

	ks = kp_vm_new_thread(mainthread, rctx);
	set_func(ks->top, fn);
	func = ks->top;
	incr_top(ks);

	ks->current_event = e;

	kp_vm_call(ks, func, 0);

	ks->current_event = NULL;
	kp_vm_exit_thread(ks);
}

/*
 * Callback tracing function for perf event subsystem.
 *
 * make ktap reentrant, don't disable irq in callback function,
 * same as perf and ftrace. to make reentrant, we need some
 * percpu data to be context isolation(irq/sirq/nmi/process)
 *
 * The recursion checking in here is mainly purpose for avoiding
 * corrupt ktap_state_t with timer closure callback. For tracepoint
 * recusion, perf core already handle it.
 *
 * Note tracepoint handler is calling with rcu_read_lock.
 */
static void perf_callback(struct perf_event *perf_event,
			   struct perf_sample_data *data,
			   struct pt_regs *regs)
{
	struct ktap_event *event;
	struct ktap_event_data e;
	ktap_state_t *ks;
	int rctx;

	event = perf_event->overflow_handler_context;
	ks = event->ks;

	if (unlikely(ks->stop))
		return;

	rctx = get_recursion_context(ks);
	if (unlikely(rctx < 0))
		return;

	e.event = event;
	e.data = data;
	e.regs = regs;
	e.argstr = NULL;

	call_probe_closure(ks, event->fn, &e, rctx);

	put_recursion_context(ks, rctx);
}

/*
 * Generic ktap event creation function (based on perf callback)
 * purpose for tracepoints/kprobe/uprobe/profile-timer/hw_breakpoint/pmu.
 */
int kp_event_create(ktap_state_t *ks, struct perf_event_attr *attr,
		    struct task_struct *task, const char *filter,
		    ktap_func_t *fn)
{
	struct ktap_event *event;
	struct perf_event *perf_event;
	void *callback = perf_callback;
	int cpu, ret;

	if (G(ks)->parm->dry_run)
		callback = NULL;

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
		event = kzalloc(sizeof(struct ktap_event), GFP_KERNEL);
		if (!event)
			return -ENOMEM;

		event->type = KTAP_EVENT_TYPE_PERF;
		event->ks = ks;
		event->fn = fn;
		perf_event = perf_event_create_kernel_counter(attr, cpu, task,
							      callback, event);
		if (IS_ERR(perf_event)) {
			int err = PTR_ERR(perf_event);
			kp_error(ks, "unable register perf event: "
				     "[cpu: %d; id: %d; err: %d]\n",
				     cpu, attr->config, err);
			kfree(event);
			return err;
		}

		if (attr->type == PERF_TYPE_TRACEPOINT) {
			const char *name = perf_event->tp_event->name;
			kp_verbose_printf(ks, "enable perf event: "
					      "[cpu: %d; id: %d; name: %s; "
					      "filter: %s; pid: %d]\n",
					      cpu, attr->config, name, filter,
					      task ? task_tgid_vnr(task) : -1);
		} else if (attr->type == PERF_TYPE_SOFTWARE &&
			 attr->config == PERF_COUNT_SW_CPU_CLOCK) {
			kp_verbose_printf(ks, "enable profile event: "
					      "[cpu: %d; sample_period: %d]\n",
					      cpu, attr->sample_period);
		} else {
			kp_verbose_printf(ks, "unknown perf event type\n");
		}

		event->perf = perf_event;
		INIT_LIST_HEAD(&event->list);
		list_add_tail(&event->list, &G(ks)->events_head);

		if (init_event_fields(ks, event)) {
			kp_error(ks, "unable init event fields id %d\n",
					attr->config);
			perf_event_release_kernel(event->perf);
			list_del(&event->list);
			kfree(event);
			return ret;
		}

		if (!filter)
			continue;

		ret = kp_ftrace_profile_set_filter(perf_event, attr->config,
						   filter);
		if (ret) {
			kp_error(ks, "unable set event filter: "
				     "[id: %d; filter: %s; ret: %d]\n",
				     attr->config, filter, ret);
			perf_event_release_kernel(event->perf);
			list_del(&event->list);
			kfree(event);
			return ret;
		}
	}

	return 0;
}

/*
 * tracepoint_probe_register functions changed prototype by introduce
 * 'struct tracepoint', this cause hard to refer tracepoint by name.
 * And these ktap raw tracepoint interface is not courage to use, so disable
 * it now.
 */
#if 0
/*
 * Ignore function proto in here, just use first argument.
 */
static void probe_callback(void *__data)
{
	struct ktap_event *event = __data;
	ktap_state_t *ks = event->ks;
	struct ktap_event_data e;
	struct pt_regs regs; /* pt_regs maybe is large for stack */
	int rctx;

	if (unlikely(ks->stop))
		return;

	rctx = get_recursion_context(ks);
	if (unlikely(rctx < 0))
		return;

	perf_fetch_caller_regs(&regs);

	e.event = event;
	e.regs = &regs;
	e.argstr = NULL;

	call_probe_closure(ks, event->fn, &e, rctx);

	put_recursion_context(ks, rctx);
}

/*
 * syscall events handling
 */

static DEFINE_MUTEX(syscall_trace_lock);
static DECLARE_BITMAP(enabled_enter_syscalls, NR_syscalls);
static DECLARE_BITMAP(enabled_exit_syscalls, NR_syscalls);
static int sys_refcount_enter;
static int sys_refcount_exit;

static int get_syscall_num(const char *name)
{
	int i;

	for (i = 0; i < NR_syscalls; i++) {
		if (syscalls_metadata[i] &&
		    !strcmp(name, syscalls_metadata[i]->name + 4))
			return i;
	}
	return -1;
}

static void trace_syscall_enter(void *data, struct pt_regs *regs, long id)
{
	struct ktap_event *event = data;
	ktap_state_t *ks = event->ks;
	struct ktap_event_data e;
	int syscall_nr;
	int rctx;

	if (unlikely(ks->stop))
		return;

	syscall_nr = syscall_get_nr(current, regs);
	if (unlikely(syscall_nr < 0))
		return;
	if (!test_bit(syscall_nr, enabled_enter_syscalls))
		return;

	rctx = get_recursion_context(ks);
	if (unlikely(rctx < 0))
		return;

	e.event = event;
	e.regs = regs;
	e.argstr = NULL;

	call_probe_closure(ks, event->fn, &e, rctx);

	put_recursion_context(ks, rctx);
}

static void trace_syscall_exit(void *data, struct pt_regs *regs, long id)
{
	struct ktap_event *event = data;
	ktap_state_t *ks = event->ks;
	struct ktap_event_data e;
	int syscall_nr;
	int rctx;

	syscall_nr = syscall_get_nr(current, regs);
	if (unlikely(syscall_nr < 0))
		return;
	if (!test_bit(syscall_nr, enabled_exit_syscalls))
		return;

	if (unlikely(ks->stop))
		return;

	rctx = get_recursion_context(ks);
	if (unlikely(rctx < 0))
		return;

	e.event = event;
	e.regs = regs;
	e.argstr = NULL;

	call_probe_closure(ks, event->fn, &e, rctx);

	put_recursion_context(ks, rctx);
}

/* called in dry-run mode, purpose for compare overhead with normal vm call */
static void dry_run_callback(void *data, struct pt_regs *regs, long id)
{

}

static void init_syscall_event_fields(struct ktap_event *event, int is_enter)
{
	struct ftrace_event_call *event_call;
	struct ktap_event_field *event_fields = &event->fields[0];
	struct syscall_metadata *meta = syscalls_metadata[event->syscall_nr];
	int idx = 0;

	event_call = is_enter ? meta->enter_event : meta->exit_event;

	event_fields[0].type = KTAP_EVENT_FIELD_TYPE_CONST;
	event_fields[0].offset = event->syscall_nr;

	if (!is_enter) {
#ifdef CONFIG_X86_64
		event_fields[1].type = KTAP_EVENT_FIELD_TYPE_REGESTER;
		event_fields[1].offset = offsetof(struct pt_regs, ax);
#endif
		return;
	}

	while (idx++ < meta->nb_args) {
		event_fields[idx].type = KTAP_EVENT_FIELD_TYPE_REGESTER;
#ifdef CONFIG_X86_64
		switch (idx) {
		case 1:
			event_fields[idx].offset = offsetof(struct pt_regs, di);
			break;
		case 2:
			event_fields[idx].offset = offsetof(struct pt_regs, si);
			break;
		case 3:
			event_fields[idx].offset = offsetof(struct pt_regs, dx);
			break;
		case 4:
			event_fields[idx].offset =
						offsetof(struct pt_regs, r10);
			break;
		case 5:
			event_fields[idx].offset = offsetof(struct pt_regs, r8);
			break;
		case 6:
			event_fields[idx].offset = offsetof(struct pt_regs, r9);
			break;
		}
#else
#warning "don't support syscall tracepoint event register access in this arch, use 'trace syscalls:* {}' instead"
		break;
#endif
	}

	/* init all rest fields as NIL */
	while (idx < 9)
		event_fields[idx++].type = KTAP_EVENT_FIELD_TYPE_NIL;
}

static int syscall_event_register(ktap_state_t *ks, const char *event_name,
				  struct ktap_event *event)
{
	int syscall_nr = 0, is_enter = 0;
	void *callback = NULL;
	int ret = 0;

	if (!strncmp(event_name, "sys_enter_", 10)) {
		is_enter = 1;
		event->type = KTAP_EVENT_TYPE_SYSCALL_ENTER;
		syscall_nr = get_syscall_num(event_name + 10);
		callback = trace_syscall_enter;
	} else if (!strncmp(event_name, "sys_exit_", 9)) {
		is_enter = 0;
		event->type = KTAP_EVENT_TYPE_SYSCALL_EXIT;
		syscall_nr = get_syscall_num(event_name + 9);
		callback = trace_syscall_exit;
	}
	
	if (G(ks)->parm->dry_run)
		callback = dry_run_callback;

	if (syscall_nr < 0)
		return -1;

	event->syscall_nr = syscall_nr;

	init_syscall_event_fields(event, is_enter);

	mutex_lock(&syscall_trace_lock);
	if (is_enter) {
		if (!sys_refcount_enter)
			ret = register_trace_sys_enter(callback, event);
		if (!ret) {
			set_bit(syscall_nr, enabled_enter_syscalls);
			sys_refcount_enter++;
		}
	} else {
		if (!sys_refcount_exit)
			ret = register_trace_sys_exit(callback, event);
		if (!ret) {
			set_bit(syscall_nr, enabled_exit_syscalls);
			sys_refcount_exit++;
		}
	}
	mutex_unlock(&syscall_trace_lock);

	return ret;
}

static int syscall_event_unregister(ktap_state_t *ks, struct ktap_event *event)
{
	int ret = 0;
	void *callback;
	
	if (event->type == KTAP_EVENT_TYPE_SYSCALL_ENTER)
		callback = trace_syscall_enter;
	else
		callback = trace_syscall_exit;

	if (G(ks)->parm->dry_run)
		callback = dry_run_callback;

	mutex_lock(&syscall_trace_lock);
	if (event->type == KTAP_EVENT_TYPE_SYSCALL_ENTER) {
		sys_refcount_enter--;
        	clear_bit(event->syscall_nr, enabled_enter_syscalls);
        	if (!sys_refcount_enter)
                	unregister_trace_sys_enter(callback, event);
	} else {
		sys_refcount_exit--;
        	clear_bit(event->syscall_nr, enabled_exit_syscalls);
        	if (!sys_refcount_exit)
                	unregister_trace_sys_exit(callback, event);
	}
	mutex_unlock(&syscall_trace_lock);

	return ret;
}

/*
 * Register tracepoint event directly, not based on perf callback
 *
 * This tracing method would be more faster than perf callback,
 * because it won't need to write trace data into any temp buffer,
 * and code path is much shorter than perf callback.
 */
int kp_event_create_tracepoint(ktap_state_t *ks, const char *event_name,
			       ktap_func_t *fn)
{
	struct ktap_event *event;
	void *callback = probe_callback;
	int is_syscall = 0;
	int ret;

	if (G(ks)->parm->dry_run)
		callback = NULL;

	if (!strncmp(event_name, "sys_enter_", 10) ||
	    !strncmp(event_name, "sys_exit_", 9))
		is_syscall = 1;

	event = kzalloc(sizeof(struct ktap_event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	event->ks = ks;
	event->fn = fn;
	event->name = kp_str_newz(ks, event_name);
	if (unlikely(!event->name)) {
		kfree(event);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&event->list);
	list_add_tail(&event->list, &G(ks)->events_head);

	if (is_syscall) {
		ret = syscall_event_register(ks, event_name, event);
	} else {
		event->type = KTAP_EVENT_TYPE_TRACEPOINT;
		ret = tracepoint_probe_register(event_name, callback, event);
	}

	if (ret) {
		kp_error(ks, "register tracepoint %s failed, ret: %d\n",
				event_name, ret);
		list_del(&event->list);
		kfree(event);
		return ret;
	}
	return 0;
}

#endif

/* kprobe handler */
static int __kprobes pre_handler_kprobe(struct kprobe *p, struct pt_regs *regs)
{
	struct ktap_event *event = container_of(p, struct ktap_event, kp);
	ktap_state_t *ks = event->ks;
	struct ktap_event_data e;
	int rctx;

	if (unlikely(ks->stop))
		return 0;

	rctx = get_recursion_context(ks);
	if (unlikely(rctx < 0))
		return 0;

	e.event = event;
	e.regs = regs;
	e.argstr = NULL;

	call_probe_closure(ks, event->fn, &e, rctx);

	put_recursion_context(ks, rctx);
	return 0;
}

/*
 * Register kprobe event directly, not based on perf callback
 *
 * This tracing method would be more faster than perf callback,
 * because it won't need to write trace data into any temp buffer,
 * and code path is much shorter than perf callback.
 */
int kp_event_create_kprobe(ktap_state_t *ks, const char *event_name,
			   ktap_func_t *fn)
{
	struct ktap_event *event;
	void *callback = pre_handler_kprobe;
	int ret;

	if (G(ks)->parm->dry_run)
		callback = NULL;

	event = kzalloc(sizeof(struct ktap_event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	event->ks = ks;
	event->fn = fn;
	event->name = kp_str_newz(ks, event_name);
	if (unlikely(!event->name)) {
		kfree(event);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&event->list);
	list_add_tail(&event->list, &G(ks)->events_head);

	event->type = KTAP_EVENT_TYPE_KPROBE;

	event->kp.symbol_name = event_name;
	event->kp.pre_handler = callback;
	ret = register_kprobe(&event->kp);
	if (ret) {
		kp_error(ks, "register kprobe event %s failed, ret: %d\n",
				event_name, ret);
		list_del(&event->list);
		kfree(event);
		return ret;
	}
	return 0;
}


static void events_destroy(ktap_state_t *ks)
{
	struct ktap_event *event;
	struct list_head *tmp, *pos;
	struct list_head *head = &G(ks)->events_head;

	list_for_each(pos, head) {
		event = container_of(pos, struct ktap_event,
					   list);
		if (event->type == KTAP_EVENT_TYPE_PERF)
			perf_event_release_kernel(event->perf);
#if 0
		else if (event->type == KTAP_EVENT_TYPE_TRACEPOINT)
			tracepoint_probe_unregister(getstr(event->name),
						    probe_callback, event);
		else if (event->type == KTAP_EVENT_TYPE_SYSCALL_ENTER ||
			 event->type == KTAP_EVENT_TYPE_SYSCALL_EXIT )
			syscall_event_unregister(ks, event);
#endif
		else if (event->type == KTAP_EVENT_TYPE_KPROBE)
			unregister_kprobe(&event->kp);
        }
       	/*
	 * Ensure our callback won't be called anymore. The buffers
	 * will be freed after that.
	 */
	tracepoint_synchronize_unregister();

	list_for_each_safe(pos, tmp, head) {
		event = container_of(pos, struct ktap_event,
					   list);
		list_del(&event->list);
		kfree(event);
	}
}

void kp_events_exit(ktap_state_t *ks)
{
	if (!G(ks)->trace_enabled)
		return;

	events_destroy(ks);

	/* call trace_end_closure after all event unregistered */
	if ((G(ks)->state != KTAP_ERROR) && G(ks)->trace_end_closure) {
		G(ks)->state = KTAP_TRACE_END;
		set_func(ks->top, G(ks)->trace_end_closure);
		incr_top(ks);
		kp_vm_call(ks, ks->top - 1, 0);
		G(ks)->trace_end_closure = NULL;
	}

	G(ks)->trace_enabled = 0;
}

int kp_events_init(ktap_state_t *ks)
{
	G(ks)->trace_enabled = 1;
	return 0;
}

