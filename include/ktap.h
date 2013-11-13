#ifndef __KTAP_H__
#define __KTAP_H__

#include "ktap_types.h"
#include "ktap_opcodes.h"

#include <linux/version.h>
#include <linux/hardirq.h>
#include <linux/trace_seq.h>

typedef struct ktap_Reg {
        const char *name;
        ktap_cfunction func;
} ktap_Reg;

struct ktap_probe_event {
	struct list_head list;
	struct perf_event *perf;
	ktap_state *ks;
	ktap_closure *cl;
};

/* this structure allocate on stack */
struct ktap_event {
	struct ktap_probe_event *pevent;
	struct ftrace_event_call *call;
	struct trace_entry *entry;
	int entry_size;
	struct pt_regs *regs;
};

#define KTAP_PERCPU_BUFFER_SIZE	(3 * PAGE_SIZE)

int gettimeofday_us(void);
ktap_state *kp_newstate(struct ktap_parm *parm, struct dentry *dir);
void kp_exit(ktap_state *ks);
void kp_final_exit(ktap_state *ks);
ktap_state *kp_newthread(ktap_state *mainthread);
void kp_exitthread(ktap_state *ks);
ktap_closure *kp_load(ktap_state *ks, unsigned char *buff);
void kp_call(ktap_state *ks, StkId func, int nresults);
void kp_optimize_code(ktap_state *ks, int level, ktap_proto *f);
void kp_register_lib(ktap_state *ks, const char *libname,
			const ktap_Reg *funcs);
void kp_init_baselib(ktap_state *ks);
void kp_init_oslib(ktap_state *ks);
void kp_init_kdebuglib(ktap_state *ks);
void kp_init_timerlib(ktap_state *ks);
void kp_init_ansilib(ktap_state *ks);

int kp_probe_init(ktap_state *ks);
void kp_probe_exit(ktap_state *ks);

void kp_perf_event_register(ktap_state *ks, struct perf_event_attr *attr,
			    struct task_struct *task, char *filter,
			    ktap_closure *cl);

void kp_event_getarg(ktap_state *ks, ktap_value *ra, int n);
void kp_event_tostring(ktap_state *ks, struct trace_seq *seq);

int kp_strfmt(ktap_state *ks, struct trace_seq *seq);

void kp_transport_write(ktap_state *ks, const void *data, size_t length);
void kp_transport_event_write(ktap_state *ks, struct ktap_event *e);
void kp_transport_print_backtrace(ktap_state *ks, int skip, int max_entries);
void *kp_transport_reserve(ktap_state *ks, size_t length);
void kp_transport_exit(ktap_state *ks);
int kp_transport_init(ktap_state *ks, struct dentry *dir);

void kp_exit_timers(ktap_state *ks);

extern int kp_max_exec_count;

/* get from kernel/trace/trace.h */
static __always_inline int trace_get_context_bit(void)
{
	int bit;

	if (in_interrupt()) {
		if (in_nmi())
			bit = 0;
		else if (in_irq())
			bit = 1;
		else
			bit = 2;
	} else
		bit = 3;

	return bit;
}

static __always_inline int get_recursion_context(ktap_state *ks)
{
	int rctx = trace_get_context_bit();
	int *val = __this_cpu_ptr(G(ks)->recursion_context[rctx]);

	if (*val)
		return -1;

	*val = true;
	barrier();

	return rctx;
}

static inline void put_recursion_context(ktap_state *ks, int rctx)
{
	int *val = __this_cpu_ptr(G(ks)->recursion_context[rctx]);

	barrier();
	*val = false;
}

static inline void *kp_percpu_data(ktap_state *ks, int type)
{
	return this_cpu_ptr(G(ks)->pcpu_data[type][trace_get_context_bit()]);
}

extern unsigned int kp_stub_exit_instr;

static inline void set_next_as_exit(ktap_state *ks)
{
	ktap_callinfo *ci;

	ci = ks->ci;
	if (!ci)
		return;

	ci->u.l.savedpc = &kp_stub_exit_instr;

	/* See precall, ci changed to ci->prev after invoke C function */
	if (ci->prev) {
		ci = ci->prev;
		ci->u.l.savedpc = &kp_stub_exit_instr;
	}
}

#define kp_verbose_printf(ks, ...) \
	if (G(ks)->parm->verbose)	\
		kp_printf(ks, "[verbose] "__VA_ARGS__);

/* get argument operation macro */
#define kp_arg(ks, n)	((ks)->ci->func + (n))
#define kp_arg_nr(ks)	((int)(ks->top - (ks->ci->func + 1)))

#define kp_arg_check(ks, narg, type)				\
	do {							\
		if (unlikely(ttypenv(kp_arg(ks, narg)) != type)) {	\
			kp_error(ks, "wrong type of argument %d\n", narg);\
			return -1;				\
		}						\
	} while (0)


#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 5, 0)
#define SPRINT_SYMBOL	sprint_symbol_no_offset
#else
#define SPRINT_SYMBOL	sprint_symbol
#endif

#endif /* __KTAP_H__ */
