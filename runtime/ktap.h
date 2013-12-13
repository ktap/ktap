#ifndef __KTAP_H__
#define __KTAP_H__

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

int kp_init_baselib(ktap_state *ks);
int kp_init_oslib(ktap_state *ks);
int kp_init_kdebuglib(ktap_state *ks);
int kp_init_timerlib(ktap_state *ks);
int kp_init_tablelib(ktap_state *ks);
int kp_init_ansilib(ktap_state *ks);
#ifdef CONFIG_KTAP_FFI
int kp_init_ffilib(ktap_state *ks);
#else
static inline int kp_init_ffilib(ktap_state *ks)
{
	return 0;
}
#endif


int kp_probe_init(ktap_state *ks);
void kp_probe_exit(ktap_state *ks);

void kp_perf_event_register(ktap_state *ks, struct perf_event_attr *attr,
			    struct task_struct *task, char *filter,
			    ktap_closure *cl);

void kp_event_getarg(ktap_state *ks, ktap_value *ra, int n);
void kp_event_tostring(ktap_state *ks, struct trace_seq *seq);
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

void kp_printf(ktap_state *ks, const char *fmt, ...);
void __kp_puts(ktap_state *ks, const char *str);
void __kp_bputs(ktap_state *ks, const char *str);

#define kp_puts(ks, str) ({						\
	static const char *trace_printk_fmt				\
		__attribute__((section("__trace_printk_fmt"))) =	\
		__builtin_constant_p(str) ? str : NULL;			\
									\
	if (__builtin_constant_p(str))					\
		__kp_bputs(ks, trace_printk_fmt);		\
	else								\
		__kp_puts(ks, str);		\
})

#endif /* __KTAP_H__ */
