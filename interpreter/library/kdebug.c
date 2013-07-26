/*
 * kdebug.c - ktap probing core implementation
 *
 * Copyright 2013 The ktap Project Developers.
 * See the COPYRIGHT file at the top-level directory of this distribution.
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
#include <linux/ctype.h>
#include <linux/version.h>
#include <linux/perf_event.h>
#include <linux/ftrace_event.h>
#include <asm/syscall.h> //syscall_set_return_value defined here
#include "../../include/ktap.h"

static void ktap_call_probe_closure(ktap_state *mainthread, ktap_closure *cl,
				    struct ktap_event *e)
{
	ktap_state *ks;
	ktap_value *func;

	ks = kp_newthread(mainthread);
	setcllvalue(ks->top, cl);
	func = ks->top;
	incr_top(ks);

	if (cl->l.p->numparams) {
		setevalue(ks->top, e);
		incr_top(ks);
	}

	ks->current_event = e;

	kp_call(ks, func, 0);

	ks->current_event = NULL;
	kp_exitthread(ks);
}

static int event_function_tostring(ktap_state *ks)
{
	struct ktap_event *e = ks->current_event;
	struct trace_iterator *iter;
	struct trace_event *ev;
	enum print_line_t ret = TRACE_TYPE_NO_CONSUME;

	if (!e->call) {
		setnilvalue(ks->top);
		goto out;
	}

	/* Simulate the iterator */

	/* use temp percpu buffer as trace_iterator */
	iter = kp_percpu_data(KTAP_PERCPU_DATA_BUFFER);

	trace_seq_init(&iter->seq);
	iter->ent = e->entry;

	ev = &(e->call->event);
	if (ev)
		ret = ev->funcs->trace(iter, 0, ev);

	if (ret != TRACE_TYPE_NO_CONSUME) {
		struct trace_seq *s = &iter->seq;
		int len = s->len >= PAGE_SIZE ? PAGE_SIZE - 1 : s->len;

		s->buffer[len] = '\0';
		setsvalue(ks->top, kp_tstring_newlstr_local(ks, s->buffer, len + 1));
	} else
		setnilvalue(ks->top);

 out:
	incr_top(ks);

	return 1;
}

/* e.tostring() */
static void event_tostring(ktap_state *ks, struct ktap_event *e, StkId ra)
{
	setfvalue(ra, event_function_tostring);
}

/* e.name */
static void event_name(ktap_state *ks, struct ktap_event *e, StkId ra)
{
	setsvalue(ra, kp_tstring_new(ks, e->call->name));
}

/* check pt_regs defintion in linux/arch/x86/include/asm/ptrace.h */
/* support other architecture pt_regs showing */
static void event_regstr(ktap_state *ks, struct ktap_event *e, StkId ra)
{
	struct pt_regs *regs = e->regs;
	char str[256] = {0};

#if defined(CONFIG_X86_32)
	snprintf(str, sizeof(str),
		"{ax: 0x%lx, orig_ax: 0x%lx, bx: 0x%lx, cx: 0x%lx, dx: 0x%lx, "
		"si: 0x%lx, di: 0x%lx, bp: 0x%lx, ds: 0x%lx, es: 0x%lx, fs: 0x%lx, "
		"gs: 0x%lx, ip: 0x%lx, cs: 0x%lx, flags: 0x%lx, sp: 0x%lx, ss: 0x%lx}\n",
		regs->ax, regs->orig_ax, regs->bx, regs->cx, regs->dx,
		regs->si, regs->di, regs->bp, regs->ds, regs->es, regs->fs,
		regs->gs, regs->ip, regs->cs, regs->flags, regs->sp, regs->ss);
#elif defined(CONFIG_X86_64)
	/* x86_64 pt_regs doesn't have ds, es, fs or gs. */
	snprintf(str, sizeof(str),
		"{ax: 0x%lx, orig_ax: 0x%lx, bx: 0x%lx, cx: 0x%lx, dx: 0x%lx, "
		"si: 0x%lx, di: 0x%lx, r8: 0x%lx, r9: 0x%lx, r10: 0x%lx, r11: 0x%lx, "
		"r12: 0x%lx, r13: 0x%lx, r14: 0x%lx, r15: 0x%lx, bp: 0x%lx, ip: 0x%lx, "
		"cs: 0x%lx, flags: 0x%lx, sp: 0x%lx, ss: 0x%lx}\n",
		regs->ax, regs->orig_ax, regs->bx, regs->cx, regs->dx,
		regs->si, regs->di, regs->r8, regs->r9, regs->r10, regs->r11,
		regs->r12, regs->r13, regs->r14, regs->r15, regs->bp, regs->ip,
		regs->cs, regs->flags, regs->sp, regs->ss);
#endif
	setsvalue(ra, kp_tstring_new_local(ks, str));
}

/* e.retval */
static void event_retval(ktap_state *ks, struct ktap_event *e, StkId ra)
{
	struct pt_regs *regs = e->regs;

	setnvalue(ra, regs_return_value(regs));
}

static int ktap_function_set_retval(ktap_state *ks)
{
	struct ktap_event *e = ks->current_event;
	int n = nvalue(kp_arg(ks, 1));

	/* use syscall_set_return_value as generic return value set macro */
	syscall_set_return_value(current, e->regs, n, 0);

	return 0;
}

/* e.set_retval*/
static void event_set_retval(ktap_state *ks, struct ktap_event *e, StkId ra)
{
	setfvalue(ra, ktap_function_set_retval);
}


#define ENTRY_HEADSIZE sizeof(struct trace_entry)
struct syscall_trace_enter {
	struct trace_entry      ent;
	int                     nr;
	unsigned long           args[];
};

struct syscall_trace_exit {
	struct trace_entry      ent;
	int                     nr;
	long                    ret;
};

/* e.sc_nr */
static void event_sc_nr(ktap_state *ks, struct ktap_event *e, StkId ra)
{
	struct syscall_trace_enter *entry = (struct syscall_trace_enter *)e->entry;

	setnvalue(ra, entry->nr);
}


#define EVENT_SC_ARGFUNC(n) \
static void event_sc_arg##n(ktap_state *ks, struct ktap_event *e, StkId ra)\
{ \
	struct syscall_trace_enter *entry = (struct syscall_trace_enter *)e->entry;	\
	setnvalue(ra, entry->args[n - 1]);	\
}

EVENT_SC_ARGFUNC(1)
EVENT_SC_ARGFUNC(2)
EVENT_SC_ARGFUNC(3)
EVENT_SC_ARGFUNC(4)
EVENT_SC_ARGFUNC(5)
EVENT_SC_ARGFUNC(6)

/***************************/
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

/* e.fieldnum */
static void event_fieldnum(ktap_state *ks, struct ktap_event *e, StkId ra)
{
	struct ftrace_event_field *field;
	struct list_head *head;
	int num = 0;

	head = ktap_get_fields(e->call);
	list_for_each_entry(field, head, link) {
		num++;
	}

	setnvalue(ra, num);
}

static int event_fieldn(ktap_state *ks)
{
	struct ktap_event *e = ks->current_event;
	int index = nvalue(kp_arg(ks, 1));
	struct ftrace_event_field *field;
	struct list_head *head;

	if (index <= 0) {
		setnilvalue(ks->top++);
		return 1;
	}

	/* this is very slow and not safe, fix it in future */
	head = ktap_get_fields(e->call);
	list_for_each_entry_reverse(field, head, link) {
		if (--index == 0) {
			if (field->size == 4) {
				int n = *(int *)((unsigned char *)e->entry +
					field->offset);
				setnvalue(ks->top++, n);
			}
			if (field->size == 8) {
				long n = *(long *)((unsigned char *)e->entry +
					field->offset);
				setnvalue(ks->top++, n);
			}
			return 1;
		}
	}

	setnilvalue(ks->top++);
	return 1;
}

/* e.field(N) */
static void event_field(ktap_state *ks, struct ktap_event *e, StkId ra)
{
	setfvalue(ra, event_fieldn);
}

static struct event_field_tbl {
	char *name;
	void (*func)(ktap_state *ks, struct ktap_event *e, StkId ra);	
} event_ftbl[] = {
	{"name", event_name},
	{"tostring", event_tostring},
	{"sc_nr", event_sc_nr},
	{"sc_arg1", event_sc_arg1},
	{"sc_arg2", event_sc_arg2},
	{"sc_arg3", event_sc_arg3},
	{"sc_arg4", event_sc_arg4},
	{"sc_arg5", event_sc_arg5},
	{"sc_arg6", event_sc_arg6},
	{"regstr", event_regstr},
	{"retval", event_retval},
	{"set_retval", event_set_retval},
	{"fieldnum", event_fieldnum},
	{"field", event_field}
};

int kp_event_get_index(const char *field)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(event_ftbl); i++) {
		if (!strcmp(event_ftbl[i].name, field)) {
			return i;
		}
	}

	return -1;
}

ktap_string *kp_event_get_ts(ktap_state *ks, int index)
{
	return kp_tstring_new(ks, event_ftbl[index].name);
}

void kp_event_handle(ktap_state *ks, void *e, int index, StkId ra)
{
	e = (struct ktap_event *)e;
	event_ftbl[index].func(ks, e, ra);
}


/* Callback function for perf event subsystem
 * make ktap reentrant, don't disable irq in callback function,
 * same as perf and ftrace. to make reentrant, we need some
 * percpu data to be context isolation(irq/sirq/nmi/process)
 *
 * perf callback already consider on the recursion issue,
 * so ktap don't need to check again in here.
 *
 * Note tracepoint handler is calling with rcu_read_lock.
 */
static void ktap_overflow_callback(struct perf_event *event,
				   struct perf_sample_data *data,
				   struct pt_regs *regs)
{
	struct ktap_probe_event *ktap_pevent;
	ktap_state  *ks;
	struct ktap_event e;

	if (unlikely(__this_cpu_read(kp_in_timer_closure)))
		return;

	ktap_pevent = event->overflow_handler_context;
	ks = ktap_pevent->ks;

	if (unlikely(ks->stop))
		return;

	e.pevent = ktap_pevent;
	e.call = event->tp_event;
	e.entry = data->raw->data;
	e.entry_size = data->raw->size;
	e.regs = regs;

	ktap_call_probe_closure(ks, ktap_pevent->cl, &e);
}

static void perf_destructor(struct ktap_probe_event *ktap_pevent)
{
	perf_event_release_kernel(ktap_pevent->perf);
}

static int (*kp_ftrace_profile_set_filter)(struct perf_event *event,
					   int event_id, char *filter_str);

static void start_probe_by_id(ktap_state *ks, struct task_struct *task,
			      int id, char *filter, ktap_closure *cl)
{
	struct ktap_probe_event *ktap_pevent;
	struct perf_event_attr attr;
	struct perf_event *event;
	int cpu, ret;

	kp_verbose_printf(ks, "enable tracepoint event id: %d, filter: %s\n",
				id, filter);

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_TRACEPOINT;	
	attr.config = id;
	attr.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
			   PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD;
	attr.sample_period = 1;
	attr.size = sizeof(attr);
	attr.disabled = 0;

	/*
	 * don't tracing until ktap_wait, the reason is:
	 * 1). some event may hit before apply filter
	 * 2). more simple to manage tracing thread
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
		event = perf_event_create_kernel_counter(&attr, cpu, task,
							 ktap_overflow_callback,
							 ktap_pevent);
		if (IS_ERR(event)) {
			int err = PTR_ERR(event);
			kp_printf(ks, "unable create tracepoint event %d on cpu %d, err: %d\n",
				  id, cpu, err);
			kp_free(ks, ktap_pevent);
			return;
		}

		ktap_pevent->perf = event;
		INIT_LIST_HEAD(&ktap_pevent->list);
		list_add_tail(&ktap_pevent->list, &G(ks)->probe_events_head);

		if (!filter)
			continue;

		ret = kp_ftrace_profile_set_filter(event, id, filter);
		if (ret) {
			kp_printf(ks, "Cannot set filter %s for event id %d, "
				"ret: %d\n", filter, id, ret);
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
	const char *ids_str = svalue(kp_arg(ks, 1));
	const char *start;
	ktap_value *tracefunc;
	ktap_closure *cl = NULL;
	int trace_pid = G(ks)->trace_pid;
	struct task_struct *task = NULL;
	char filter_str[128] = {0};
	char *filter, *ptr1, *sep, *ptr;

	if (kp_arg_nr(ks) >= 2) {
		tracefunc = kp_arg(ks, 2);

		if (ttisfunc(tracefunc))
			cl = (ktap_closure *)gcvalue(tracefunc);
	}

	if (!cl)
		return -1;

	if (trace_pid != -1) {
		task = pid_task(find_vpid(trace_pid), PIDTYPE_PID);
		if (!task) {
			kp_printf(ks, "Error: cannot find pid %d\n", trace_pid);
			return -1;
		}
	}

	start = ids_str;

 again:
	filter = NULL;

	sep = strchr(start, ';');
	if (!sep)
		ptr1 = strchr(start, '/');
	else
		ptr1 = strnchr(start, sep - start, '/');

	if (ptr1) {
		char *ptr2 = strrchr(ptr1 + 1, '/');

		if (ptr2) {
			memset(filter_str, 0, sizeof(filter_str));
			strncpy(filter_str, ptr1 + 1, ptr2 - ptr1 - 1);
			filter = &filter_str[0];
		} else {
			kp_printf(ks, "cannot parse ids_str: %s\n", ids_str);
			return -1;
		}
	}

	for (ptr = start; *ptr != ';' && *ptr != '\0' && *ptr != '/'; ptr++) {
		char token[32] = {0};
		int id;
		int i = 0;

		if (*ptr == ' ')
			continue;

		while (isdigit(*ptr)) {
			token[i++] = *ptr++;
		}

		if (!kstrtoint(token, 10, &id))
			start_probe_by_id(ks, task, id, filter, cl);
	}

	if (sep && (*(sep + 1) != '\0')) {
		start = sep + 1;
		goto again;
	}

	return 0;
}

static int ktap_lib_probe_end(ktap_state *ks)
{
	ktap_value *endfunc;

	if (kp_arg_nr(ks) == 0)
		return 0;

	endfunc = kp_arg(ks, 1);

	G(ks)->trace_end_closure = clvalue(endfunc);
	return 0;
}

static int ktap_lib_traceoff(ktap_state *ks)
{
	end_probes(ks);

	/* call trace_end_closure after probed end */
	if (G(ks)->trace_end_closure) {
		setcllvalue(ks->top, G(ks)->trace_end_closure);
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

	if (!list_empty(&G(ks)->probe_events_head) ||
		!list_empty(&G(ks)->timers))
		kp_wait(ks);

	end_probes(ks);

	/* call trace_end_closure after probed end */
	if (G(ks)->trace_end_closure) {
		setcllvalue(ks->top, G(ks)->trace_end_closure);
		incr_top(ks);
		kp_call(ks, ks->top - 1, 0);
		G(ks)->trace_end_closure = NULL;
	}

	G(ks)->trace_enabled = 0;
}

int kp_probe_init(ktap_state *ks)
{
	INIT_LIST_HEAD(&(G(ks)->probe_events_head));

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
		kallsyms_lookup_name("ftrace_profile_set_filter");
	if (!kp_ftrace_profile_set_filter) {
		printk("ktap: cannot lookup ftrace_profile_set_filter "
		       "in kallsyms\n");
		return;
	}

	kp_register_lib(ks, "kdebug", kdebuglib_funcs);
}

