#ifndef __KTAP_H__
#define __KTAP_H__

#include <linux/version.h>
#include <linux/hardirq.h>
#include <linux/trace_seq.h>

#ifndef raw_cpu_ptr
#define raw_cpu_ptr __this_cpu_ptr
#endif

/* for built-in library C function register */
typedef struct ktap_libfunc {
        const char *name; /* function name */
        ktap_cfunction func; /* function pointer */
} ktap_libfunc_t;

long gettimeofday_ns(void); /* common helper function */
int kp_lib_init_base(ktap_state_t *ks);
int kp_lib_init_kdebug(ktap_state_t *ks);
int kp_lib_init_timer(ktap_state_t *ks);
int kp_lib_init_table(ktap_state_t *ks);
int kp_lib_init_ansi(ktap_state_t *ks);
int kp_lib_init_net(ktap_state_t *ks);
#ifdef CONFIG_KTAP_FFI
int kp_lib_init_ffi(ktap_state_t *ks);
#endif

void kp_exit_timers(ktap_state_t *ks);
void kp_freeupval(ktap_state_t *ks, ktap_upval_t *uv);

extern int (*kp_ftrace_profile_set_filter)(struct perf_event *event,
					   int event_id,
					   const char *filter_str);

extern struct syscall_metadata **syscalls_metadata;

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

static __always_inline int get_recursion_context(ktap_state_t *ks)
{
	int rctx = trace_get_context_bit();
	int *val = raw_cpu_ptr(G(ks)->recursion_context[rctx]);

	if (*val)
		return -1;

	*val = true;
	return rctx;
}

static inline void put_recursion_context(ktap_state_t *ks, int rctx)
{
	int *val = raw_cpu_ptr(G(ks)->recursion_context[rctx]);
	*val = false;
}

static inline void *kp_this_cpu_state(ktap_state_t *ks, int rctx)
{
	return this_cpu_ptr(G(ks)->percpu_state[rctx]);
}

static inline void *kp_this_cpu_print_buffer(ktap_state_t *ks)
{
	return this_cpu_ptr(G(ks)->percpu_print_buffer[trace_get_context_bit()]);
}

static inline void *kp_this_cpu_temp_buffer(ktap_state_t *ks)
{
	return this_cpu_ptr(G(ks)->percpu_temp_buffer[trace_get_context_bit()]);
}

#define kp_verbose_printf(ks, ...) \
	if (G(ks)->parm->verbose)	\
		kp_printf(ks, "[verbose] "__VA_ARGS__);

/* argument operation macro */
#define kp_arg(ks, idx)	((ks)->func + (idx))
#define kp_arg_nr(ks)	((int)(ks->top - (ks->func + 1)))

#define kp_arg_check(ks, idx, type)				\
	do {							\
		if (unlikely(itype(kp_arg(ks, idx)) != type)) {	\
			kp_error(ks, "wrong type of argument %d\n", idx);\
			return -1;				\
		}						\
	} while (0)

#define kp_arg_checkstring(ks, idx)				\
	({							\
		ktap_val_t *o = kp_arg(ks, idx);		\
		if (unlikely(!is_string(o))) {			\
			kp_error(ks, "wrong type of argument %d\n", idx); \
			return -1;				\
		}						\
		svalue(o);					\
	})

#define kp_arg_checkfunction(ks, idx)				\
	({							\
		ktap_val_t *o = kp_arg(ks, idx);		\
		if (unlikely(!is_function(o))) {			\
			kp_error(ks, "wrong type of argument %d\n", idx); \
			return -1;				\
		}						\
		clvalue(o);					\
	})

#define kp_arg_checknumber(ks, idx)				\
	({							\
		ktap_val_t *o = kp_arg(ks, idx);		\
		if (unlikely(!is_number(o))) {			\
			kp_error(ks, "wrong type of argument %d\n", idx); \
			return -1;				\
		}						\
		nvalue(o);					\
	})

#define kp_arg_checkoptnumber(ks, idx, def)			\
	({							\
		ktap_number n;					\
		if (idx > kp_arg_nr(ks)) {				\
			n = def;				\
		} else {					\
			ktap_val_t *o = kp_arg(ks, idx);	\
			if (unlikely(!is_number(o))) {		\
				kp_error(ks, "wrong type of argument %d\n", \
					     idx);		\
				return -1;			\
			}					\
			n = nvalue(o);				\
		}						\
		n;						\
	})

#define kp_error(ks, args...)			\
	do {					\
		kp_printf(ks, "error: "args);	\
		kp_vm_try_to_exit(ks);		\
		G(ks)->state = KTAP_ERROR;	\
	} while(0)


/* TODO: this code need to cleanup */
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 5, 0)
#define SPRINT_SYMBOL	sprint_symbol_no_offset
#else
#define SPRINT_SYMBOL	sprint_symbol
#endif

extern int kp_max_loop_count;

void kp_printf(ktap_state_t *ks, const char *fmt, ...);
void __kp_puts(ktap_state_t *ks, const char *str);
void __kp_bputs(ktap_state_t *ks, const char *str);

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

#define err2msg(em)     (kp_err_allmsg+(int)(em))
extern const char *kp_err_allmsg;

#endif /* __KTAP_H__ */

