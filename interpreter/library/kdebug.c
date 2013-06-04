/*
 * kdebug.c - ktap probing core implementation
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
#include <linux/version.h>
#include <linux/perf_event.h>
#include <linux/ftrace_event.h>
#include <linux/kprobes.h>
#include <asm/syscall.h> //syscall_set_return_value defined here
#include "../../include/ktap.h"

/* get argument operation macro */
#define GetArg(ks, n)	((ks)->ci->func + (n))
#define GetArgN(ks)	((int)(ks->top - (ks->ci->func + 1)))

/* this structure allocate on stack */
struct ktap_trace_arg {
	ktap_State *ks;
	Closure *cl;
};

/* this structure allocate on stack */
struct ktap_event {
	struct ktap_probe_event *pevent;
	struct ftrace_event_call *call;
	void *entry;
	int entry_size;
	struct pt_regs *regs;
	int type;
};

struct ktap_probe_event {
	struct list_head list;
	int type;
	const char *name;
	union {
		struct perf_event *perf;
		struct kretprobe p;
	} u;
	ktap_State *ks;
	Closure *cl;
	void (*destructor)(struct ktap_probe_event *ktap_pevent);
};

enum {
	EVENT_TYPE_DEFAULT = 0,
	EVENT_TYPE_SYSCALL_ENTER,
	EVENT_TYPE_SYSCALL_EXIT,
	EVENT_TYPE_TRACEPOINT_MAX,
	EVENT_TYPE_KPROBE,
	EVENT_TYPE_KRETPROBE,
};

DEFINE_PER_CPU(bool, ktap_in_tracing);

static void ktap_call_probe_closure(ktap_State *mainthread, Closure *cl,
				    struct ktap_event *e)
{
	ktap_State *ks;
	Tvalue *func;

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

/* kprobe handler is called in interrupt disabled? */
static int __kprobes pre_handler_kprobe(struct kprobe *p, struct pt_regs *regs)
{
	struct ktap_probe_event *ktap_pevent;
	struct ktap_event e;

	if (unlikely(__this_cpu_read(ktap_in_tracing)))
		return 0;

	__this_cpu_write(ktap_in_tracing, true);

	ktap_pevent = container_of(p, struct ktap_probe_event, u.p.kp);

	e.pevent = ktap_pevent;
	e.call = NULL;
	e.entry = NULL;
	e.entry_size = 0;
	e.regs = regs;
	e.type = ktap_pevent->type;

	if (same_thread_group(current, G(ktap_pevent->ks)->task))
		goto out;

	ktap_call_probe_closure(ktap_pevent->ks, ktap_pevent->cl, &e);

 out:
	__this_cpu_write(ktap_in_tracing, false);
	return 0;
}

static void kprobe_destructor(struct ktap_probe_event *ktap_pevent)
{
	unregister_kprobe(&ktap_pevent->u.p.kp);
}

/* kretprobe handler is called in interrupt disabled? */
static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct ktap_probe_event *ktap_pevent;
	struct kretprobe *rp = ri->rp;
	struct ktap_event e;
	
	if (unlikely(__this_cpu_read(ktap_in_tracing)))
		return 0;

	__this_cpu_write(ktap_in_tracing, true);

	ktap_pevent = container_of(rp, struct ktap_probe_event, u.p);

	e.pevent = ktap_pevent;
	e.call = NULL;
	e.entry = NULL;
	e.entry_size = 0;
	e.regs = regs;
	e.type = ktap_pevent->type;

	if (same_thread_group(current, G(ktap_pevent->ks)->task))
		goto out;

	ktap_call_probe_closure(ktap_pevent->ks, ktap_pevent->cl, &e);

 out:
	__this_cpu_write(ktap_in_tracing, false);
	return 0;
}

static void kretprobe_destructor(struct ktap_probe_event *ktap_pevent)
{
	unregister_kretprobe(&ktap_pevent->u.p);
	kp_free(ktap_pevent->ks, ktap_pevent->u.p.kp.symbol_name);
}

static int start_kprobe(ktap_State *ks, const char *event_name, Closure *cl)
{
	struct ktap_probe_event *ktap_pevent;
	int len = strlen(event_name);

	ktap_pevent = kp_zalloc(ks, sizeof(*ktap_pevent));
	ktap_pevent->ks = ks;
	ktap_pevent->cl = cl;

	INIT_LIST_HEAD(&ktap_pevent->list);
	list_add_tail(&ktap_pevent->list, &G(ks)->probe_events_head);

	ktap_pevent->name = event_name;

	if (event_name[len - 1] == '!') {
		char *symbol_name = kstrdup(event_name, GFP_KERNEL);
		symbol_name[len - 1] = '\0';

		ktap_pevent->type = EVENT_TYPE_KRETPROBE;
		ktap_pevent->u.p.kp.symbol_name = (const char *)symbol_name;
		ktap_pevent->u.p.handler = ret_handler;
		ktap_pevent->destructor = kretprobe_destructor;
		if (register_kretprobe(&ktap_pevent->u.p)) {
			kp_free(ks, symbol_name);
			kp_printf(ks, "Cannot register retprobe: %s\n", event_name);
			goto error;
		}
	} else {
		ktap_pevent->type = EVENT_TYPE_KPROBE;
		ktap_pevent->u.p.kp.symbol_name = event_name;
		ktap_pevent->u.p.kp.pre_handler = pre_handler_kprobe;
		ktap_pevent->u.p.kp.post_handler = NULL;
		ktap_pevent->u.p.kp.fault_handler = NULL;
		ktap_pevent->u.p.kp.break_handler = NULL;
		ktap_pevent->destructor = kprobe_destructor;
		if (register_kprobe(&ktap_pevent->u.p.kp)) {
			kp_printf(ks, "Cannot register probe: %s\n", event_name);
			goto error;
		}
	}

	return 0;

 error:
	list_del(&ktap_pevent->list);
	kp_free(ks, ktap_pevent);
	return -1;
}

static struct trace_iterator *percpu_trace_iterator;
static int event_function_tostring(ktap_State *ks)
{
	struct ktap_event *e = ks->current_event;
	struct trace_iterator *iter;
	struct trace_event *ev;
	enum print_line_t ret = TRACE_TYPE_NO_CONSUME;

	/* Simulate the iterator */

	/* iter can be a bit big for the stack, use percpu*/
	iter = per_cpu_ptr(percpu_trace_iterator, smp_processor_id());

	trace_seq_init(&iter->seq);
	iter->ent = e->entry;

	ev = &(e->call->event);
	if (ev)
		ret = ev->funcs->trace(iter, 0, ev);

	if (ret != TRACE_TYPE_NO_CONSUME) {
		struct trace_seq *s = &iter->seq;
		int len = s->len >= PAGE_SIZE ? PAGE_SIZE - 1 : s->len;

		s->buffer[len] = '\0';
		setsvalue(ks->top, kp_tstring_assemble(ks, s->buffer, len + 1));
	} else
		setnilvalue(ks->top);

	incr_top(ks);

	return 1;
}

static void event_tostring(ktap_State *ks, struct ktap_event *e, StkId ra)
{
	if (e->type >= EVENT_TYPE_TRACEPOINT_MAX)
		setnilvalue(ra);
		
	setfvalue(ra, event_function_tostring);
}
/*
 * called when print(e)
 */
void kp_show_event(ktap_State *ks)
{
	event_function_tostring(ks);
	ks->top--;
	kp_printf(ks, "%s", getstr(rawtsvalue(ks->top)));
}

static void event_name(ktap_State *ks, struct ktap_event *e, StkId ra)
{
	setsvalue(ra, kp_tstring_new(ks, e->pevent->name));
}

static void event_print_fmt(ktap_State *ks, struct ktap_event *e, StkId ra)
{
	setsvalue(ra, kp_tstring_new(ks, e->call->print_fmt));
}

/* check pt_regs defintion in linux/arch/x86/include/asm/ptrace.h */
/* support other architecture pt_regs showing */
static void event_regstr(ktap_State *ks, struct ktap_event *e, StkId ra)
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
	setsvalue(ra, kp_tstring_new(ks, str));
}

static void event_retval(ktap_State *ks, struct ktap_event *e, StkId ra)
{
	struct pt_regs *regs = e->regs;

	if (e->type != EVENT_TYPE_KRETPROBE) {
		setnilvalue(ra);
		return;
	}

	setnvalue(ra, regs_return_value(regs));
}

static int ktap_function_set_retval(ktap_State *ks)
{
	struct ktap_event *e = ks->current_event;
	int n = nvalue(GetArg(ks, 1));

	/* use syscall_set_return_value as generic return value set macro */
	syscall_set_return_value(current, e->regs, n, 0);

	return 0;
}

static void event_set_retval(ktap_State *ks, struct ktap_event *e, StkId ra)
{
	if (e->type != EVENT_TYPE_KRETPROBE && e->type != EVENT_TYPE_SYSCALL_EXIT) {
		setnilvalue(ra);
		return;
	}
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

static void event_sc_nr(ktap_State *ks, struct ktap_event *e, StkId ra)
{
	struct syscall_trace_enter *entry = e->entry;

	if (e->type != EVENT_TYPE_SYSCALL_ENTER) {
		setnilvalue(ra);
		return;
	}

	setnvalue(ra, entry->nr);
}

static void event_sc_is_enter(ktap_State *ks, struct ktap_event *e, StkId ra)
{
	if (e->type == EVENT_TYPE_SYSCALL_ENTER) {
		setbvalue(ra, 1);
	} else {
		setbvalue(ra, 0);
	}
}


#define EVENT_SC_ARGFUNC(n) \
static void event_sc_arg##n(ktap_State *ks, struct ktap_event *e, StkId ra)\
{ \
	struct syscall_trace_enter *entry = e->entry;	\
	if (e->type != EVENT_TYPE_SYSCALL_ENTER) {	\
		setnilvalue(ra);	\
		return;	\
	}	\
	setnvalue(ra, entry->args[n - 1]);	\
}

EVENT_SC_ARGFUNC(1)
EVENT_SC_ARGFUNC(2)
EVENT_SC_ARGFUNC(3)
EVENT_SC_ARGFUNC(4)
EVENT_SC_ARGFUNC(5)
EVENT_SC_ARGFUNC(6)

/***************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)
struct ftrace_event_field {
	struct list_head        link;
	const char              *name;
	const char              *type;
	int                     filter_type;
	int                     offset;
	int                     size;
	int                     is_signed;
};
#endif

/* e.narg */
static void event_narg(ktap_State *ks, struct ktap_event *e, StkId ra)
{
	setsvalue(ra, kp_tstring_new(ks, e->call->name));
}

static struct list_head *ktap_get_fields(struct ftrace_event_call *event_call)
{
	if (!event_call->class->get_fields)
		return &event_call->class->fields;
	return event_call->class->get_fields(event_call);
}

/* e.allfield */
static void event_allfield(ktap_State *ks, struct ktap_event *e, StkId ra)
{
	char s[128];
	int len, pos = 0;
	struct ftrace_event_field *field;
	struct list_head *head;

	head = ktap_get_fields(e->call);
	list_for_each_entry_reverse(field, head, link) {
		len = sprintf(s + pos, "[%s-%s-%d-%d-%d] ", field->name, field->type,
				 field->offset, field->size, field->is_signed);
		pos += len;
	}
	s[pos] = '\0';

	setsvalue(ra, kp_tstring_new(ks, s));
}

static void event_field(ktap_State *ks, struct ktap_event *e, int index, StkId ra)
{
	struct ftrace_event_field *field;
	struct list_head *head;

	head = ktap_get_fields(e->call);
	list_for_each_entry_reverse(field, head, link) {
		if ((--index == 0) && (field->size == 4)) {
			int n = *(int *)((unsigned char *)e->entry + field->offset);
			setnvalue(ra, n);
			return;
		}
	}

	setnilvalue(ra);
}


static void event_field1(ktap_State *ks, struct ktap_event *e, StkId ra)
{
	event_field(ks, e, 1, ra);
}


#define EVENT_FIELD_BASE	100

static struct event_field_tbl {
	char *name;
	void (*func)(ktap_State *ks, struct ktap_event *e, StkId ra);	
} event_ftbl[] = {
	{"name", event_name},
	{"tostring", event_tostring},
	{"print_fmt", event_print_fmt},
	{"sc_nr", event_sc_nr},
	{"sc_is_enter", event_sc_is_enter},
	{"sc_arg1", event_sc_arg1},
	{"sc_arg2", event_sc_arg2},
	{"sc_arg3", event_sc_arg3},
	{"sc_arg4", event_sc_arg4},
	{"sc_arg5", event_sc_arg5},
	{"sc_arg6", event_sc_arg6},
	{"regstr", event_regstr},
	{"retval", event_retval},
	{"set_retval", event_set_retval},
	{"allfield", event_allfield},
	{"field1", event_field1}
};

int kp_event_get_index(const char *field)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(event_ftbl); i++) {
		if (!strcmp(event_ftbl[i].name, field)) {
			return EVENT_FIELD_BASE + i;
		}
	}

	return -1;
}

Tstring *kp_event_get_ts(ktap_State *ks, int index)
{
	return kp_tstring_new(ks, event_ftbl[index - EVENT_FIELD_BASE].name);
}

void kp_event_handle(ktap_State *ks, void *e, int index, StkId ra)
{
	e = (struct ktap_event *)e;

	if (index < EVENT_FIELD_BASE) {
		//event_field(ks, event, index, ra);
	} else
		event_ftbl[index - EVENT_FIELD_BASE].func(ks, e, ra);
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
/*
 * perf_event_enable and perf_event_disable only exported in commit
 * dcfce4a095932e6e95d83ad982be3609947963bc, which commited in Linux 3.3,
 * so we hack it in here for kernel earlier than 3.3
 * Note that ktap currently only support kernel 3.1 or later version.
 */
void perf_event_enable(struct perf_event *event)
{
	static void (*func)(struct perf_event *event);

	if (func) {
		(*func)(event);
		return;
	}

	func = kallsyms_lookup_name("perf_event_enable");
	if (!func) {
		printk("ktap: cannot lookup perf_event_enable in kallsyms\n");
		return;
	}

	(*func)(event);
}

void perf_event_disable(struct perf_event *event)
{
	static void (*func)(struct perf_event *event);

	if (func) {
		(*func)(event);
		return;
	}

	func = kallsyms_lookup_name("perf_event_disable");
	if (!func) {
		printk("ktap: cannot lookup perf_event_disable in kallsyms\n");
		return;
	}

	(*func)(event);
}
#endif


/* Callback function for perf event subsystem */
static void ktap_overflow_callback(struct perf_event *event,
				   struct perf_sample_data *data,
				   struct pt_regs *regs)
{
	struct ktap_probe_event *ktap_pevent;
	ktap_State  *ks;
	struct ktap_event e;
	unsigned long irq_flags;

	if (unlikely(__this_cpu_read(ktap_in_tracing)))
		return;

	ktap_pevent = event->overflow_handler_context;
	ks = ktap_pevent->ks;

	e.pevent = ktap_pevent;
	e.call = event->tp_event;
	e.entry = data->raw->data;
	e.entry_size = data->raw->size;
	e.regs = regs;
	e.type = ktap_pevent->type;

	local_irq_save(irq_flags);
	__this_cpu_write(ktap_in_tracing, true);

	if (same_thread_group(current, G(ks)->task))
		goto out;

	ktap_call_probe_closure(ks, ktap_pevent->cl, &e);

 out:
	__this_cpu_write(ktap_in_tracing, false);
	local_irq_restore(irq_flags);
}

static void perf_destructor(struct ktap_probe_event *ktap_pevent)
{
	perf_event_disable(ktap_pevent->u.perf);
	perf_event_release_kernel(ktap_pevent->u.perf);
}
static void enable_tracepoint_on_cpu(int cpu, struct perf_event_attr *attr,
				     struct ftrace_event_call *call,
				     struct ktap_trace_arg *arg, int type)
{
	struct ktap_probe_event *ktap_pevent;
	struct perf_event *event;

	ktap_pevent = kp_zalloc(arg->ks, sizeof(*ktap_pevent));
	ktap_pevent->name = call->name;
	ktap_pevent->ks = arg->ks;
	ktap_pevent->cl = arg->cl;
	ktap_pevent->type = type;
	ktap_pevent->destructor = perf_destructor;
	event = perf_event_create_kernel_counter(attr, cpu, NULL,
						 ktap_overflow_callback, ktap_pevent);
	if (IS_ERR(event)) {
		int err = PTR_ERR(event);
		kp_printf(arg->ks, "unable create tracepoint event %s on cpu %d, err: %d\n",
				call->name, cpu, err);
		kp_free(arg->ks, ktap_pevent);
		return;
	}

	ktap_pevent->u.perf = event;
	INIT_LIST_HEAD(&ktap_pevent->list);
	list_add_tail(&ktap_pevent->list, &G(arg->ks)->probe_events_head);

	perf_event_enable(event);
}

static void enable_tracepoint(struct ftrace_event_call *call, void *data)
{
	struct ktap_trace_arg *arg = data;
	struct perf_event_attr attr;
	int cpu, type = EVENT_TYPE_DEFAULT;

	kp_printf(arg->ks, "enable tracepoint event: %s\n", call->name);

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_TRACEPOINT;	
	attr.config = call->event.type;
	attr.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
			   PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD;
	attr.sample_period = 1;
	attr.size = sizeof(attr);

	if (!strncmp(call->name, "sys_enter_", 10)) {
		type = EVENT_TYPE_SYSCALL_ENTER;
	} else if (!strncmp(call->name, "sys_exit_", 9)) {
		type = EVENT_TYPE_SYSCALL_EXIT;
	}

	for_each_online_cpu(cpu)
		enable_tracepoint_on_cpu(cpu, &attr, call, arg, type);
}

struct list_head *ftrace_events_ptr;

typedef void (*ftrace_call_func)(struct ftrace_event_call * call, void *data);
/* helper function for ktap register tracepoint */
static void ftrace_on_event_call(const char *buf, ftrace_call_func actor,
				 void *data)
{
	char *event = NULL, *sub = NULL, *match, *buf_ptr = NULL;
	char new_buf[32] = {0};
	struct ftrace_event_call *call;
	ktap_State *ks = ((struct ktap_trace_arg *)data)->ks;
	int total = 0;

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

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 4, 0)
		if (call->flags & TRACE_EVENT_FL_IGNORE_ENABLE)
			continue;
#endif

		if (match &&
		    strcmp(match, call->name) != 0 &&
		    strcmp(match, call->class->system) != 0)
			continue;

		if (sub && strcmp(sub, call->class->system) != 0)
			continue;

		if (event && strcmp(event, call->name) != 0)
			continue;

		(*actor)(call, data);
		total++;
	}
	kp_printf(ks, "total enabled %d events\n", total);
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

static int start_probe(ktap_State *ks, const char *event_name, Closure *cl)
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
		kp_printf(ks, "unknown probe event name: %s\n", event_name);
		return -1;
	}

}

static void end_probes(struct ktap_State *ks)
{
	struct ktap_probe_event *ktap_pevent;
	struct list_head *tmp, *pos;
	struct list_head *head = &G(ks)->probe_events_head;

	list_for_each(pos, head) {
		ktap_pevent = container_of(pos, struct ktap_probe_event,
					   list);
		ktap_pevent->destructor(ktap_pevent);
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

static int ktap_lib_probe(ktap_State *ks)
{
	Tvalue *evname = GetArg(ks, 1);
	const char *event_name;
	Tvalue *tracefunc;
	Closure *cl;

	if (GetArgN(ks) >= 2) {
		tracefunc = GetArg(ks, 2);

		if (ttisfunc(tracefunc))
			cl = (Closure *)gcvalue(tracefunc);
		else
			cl = NULL;
	} else
		cl = NULL;

	if (!cl || isnil(evname))
		return -1;

	event_name = svalue(evname);
	return start_probe(ks, event_name, cl);
}

static int ktap_lib_probe_end(ktap_State *ks)
{
	Tvalue *endfunc;
	int no_wait = 0;

	if (GetArgN(ks) == 0)
		return 0;

	endfunc = GetArg(ks, 1);
	if (GetArgN(ks) >= 2)
		no_wait = nvalue(GetArg(ks, 2));

	if (!no_wait) {
		kp_printf(ks, "Press Control-C to stop.\n");
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (fatal_signal_pending(current))
			flush_signals(current);
	}

	end_probes(ks);

	/* newline for handle CTRL+C display as ^C */
	kp_printf(ks, "\n");

	setcllvalue(ks->top, clvalue(endfunc));
	incr_top(ks);
	
	kp_call(ks, ks->top - 1, 0);
	return 0;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 4, 0)
#include <trace/events/printk.h>
static DEFINE_PER_CPU(bool, ktap_in_dumpstack);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
void trace_console_func(void *__data, const char *text, unsigned start,
			unsigned end, size_t len)
#else
void trace_console_func(void *__data, const char *text, size_t len)
#endif
{
	ktap_State *ks = __data;

	if (likely(!__this_cpu_read(ktap_in_dumpstack)))
		return;

	/* cannot use kp_printf here */
	kp_transport_write(ks, text, len);
}

/*
 * Some method:
 * 1) use console tracepoint
 * 2) register console driver
 * 3) hack printk function, like kdb does now.
 * 4) console_lock firstly, read log buffer, then console_unlock
 *
 * Note we cannot use console_lock/console_trylock here.
 * Issue: printk may not call call_console_drivers if console semaphore
 * is already held, in this case, ktap may output nothing.
 *
 * todo: not output to consoles.
 */
static int ktap_lib_dumpstack(ktap_State *ks)
{
	__this_cpu_write(ktap_in_dumpstack, true);

	dump_stack();

	__this_cpu_write(ktap_in_dumpstack, false);
	return 0;
}
#else
static int ktap_lib_dumpstack(ktap_State *ks)
{
	kp_printf(ks, "your kernel version don't support ktap dumpstack\n");
	return 0;
}

#endif

void kp_probe_exit(ktap_State *ks)
{
	end_probes(ks);

	if (!G(ks)->trace_enabled)
		return;

	free_percpu(percpu_trace_iterator);

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 4, 0)
	unregister_trace_console(trace_console_func, ks);
#endif

	G(ks)->trace_enabled = 0;
}

int kp_probe_init(ktap_State *ks)
{
	INIT_LIST_HEAD(&(G(ks)->probe_events_head));

	/* get ftrace_events global variable if ftrace_events not exported */
	ftrace_events_ptr =
		(struct list_head *) kallsyms_lookup_name("ftrace_events");
	if (!ftrace_events_ptr) {
		kp_printf(ks, "cannot lookup ftrace_events in kallsyms\n");
		return -1;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 4, 0)
	if (register_trace_console(trace_console_func, ks)) {
		kp_printf(ks, "cannot register trace console\n");
		return -1;
	}
#endif

	/* allocate percpu data */
	if (!G(ks)->trace_enabled) {
		percpu_trace_iterator = alloc_percpu(struct trace_iterator);
		if (!percpu_trace_iterator)
			return -1;
	}

	G(ks)->trace_enabled = 1;
	return 0;
}


static const ktap_Reg kdebuglib_funcs[] = {
	{"dumpstack", ktap_lib_dumpstack},
	{"probe", ktap_lib_probe},
	{"probe_end", ktap_lib_probe_end},
	{NULL}
};

void kp_init_kdebuglib(ktap_State *ks)
{
	kp_register_lib(ks, "kdebug", kdebuglib_funcs);
}

