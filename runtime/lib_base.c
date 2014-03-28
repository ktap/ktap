/*
 * lib_base.c - base library
 *
 * Caveat: all kernel funtion called by ktap library have to be lock free,
 * otherwise system will deadlock.
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

#include <linux/version.h>
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/ring_buffer.h>
#include <linux/stacktrace.h>
#include <linux/cred.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
#include <linux/uidgid.h>
#endif
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_tab.h"
#include "kp_transport.h"
#include "kp_events.h"
#include "kp_vm.h"

static int kplib_print(ktap_state_t *ks)
{
	int i;
	int n = kp_arg_nr(ks);

	for (i = 1; i <= n; i++) {
		ktap_val_t *arg = kp_arg(ks, i);
		if (i > 1)
			kp_puts(ks, "\t");
		kp_obj_show(ks, arg);
	}

	kp_puts(ks, "\n");
	return 0;
}

/* don't engage with intern string in printf, use buffer directly */
static int kplib_printf(ktap_state_t *ks)
{
	struct trace_seq *seq;

	preempt_disable_notrace();

	seq = kp_this_cpu_print_buffer(ks);
	trace_seq_init(seq);

	if (kp_str_fmt(ks, seq))
		goto out;

	seq->buffer[seq->len] = '\0';
	kp_transport_write(ks, seq->buffer, seq->len + 1);

 out:
	preempt_enable_notrace();
	return 0;
}

#define HISTOGRAM_DEFAULT_TOP_NUM	20

static int kplib_print_hist(ktap_state_t *ks)
{
	int n ;

	kp_arg_check(ks, 1, KTAP_TTAB);
	n = kp_arg_checkoptnumber(ks, 2, HISTOGRAM_DEFAULT_TOP_NUM);

	n = min(n, 1000);
	n = max(n, HISTOGRAM_DEFAULT_TOP_NUM);

	kp_tab_print_hist(ks, hvalue(kp_arg(ks, 1)), n);

	return 0;
}

static int kplib_pairs(ktap_state_t *ks)
{
	kp_arg_check(ks, 1, KTAP_TTAB);

	set_cfunc(ks->top++, (ktap_cfunction)kp_tab_next);
	set_table(ks->top++, hvalue(kp_arg(ks, 1)));
	set_nil(ks->top++);
	return 3;
}

static int kplib_len(ktap_state_t *ks)
{
	int len = kp_obj_len(ks, kp_arg(ks, 1));

	if (len < 0)
		return -1;

	set_number(ks->top, len);
	incr_top(ks);
	return 1;
}

static int kplib_delete(ktap_state_t *ks)
{
	kp_arg_check(ks, 1, KTAP_TTAB);
	kp_tab_clear(hvalue(kp_arg(ks, 1)));
	return 0;
}

#ifdef CONFIG_STACKTRACE
static int kplib_stack(ktap_state_t *ks)
{
	uint16_t skip, depth = 10;

	depth = kp_arg_checkoptnumber(ks, 1, 10); /* default as 10 */
	depth = min_t(uint16_t, depth, KP_MAX_STACK_DEPTH);
	skip = kp_arg_checkoptnumber(ks, 2, 10); /* default as 10 */

	set_kstack(ks->top, depth, skip);
	incr_top(ks);
	return 1;
}
#else
static int kplib_stack(ktap_state_t *ks)
{
	kp_error(ks, "Please enable CONFIG_STACKTRACE before call stack()\n");
	return -1;
}
#endif


extern unsigned long long ns2usecs(cycle_t nsec);
static int kplib_print_trace_clock(ktap_state_t *ks)
{
	unsigned long long t;
	unsigned long secs, usec_rem;
	u64 timestamp;

	/* use ring buffer's timestamp */
	timestamp = ring_buffer_time_stamp(G(ks)->buffer, smp_processor_id());

	t = ns2usecs(timestamp);
	usec_rem = do_div(t, USEC_PER_SEC);
	secs = (unsigned long)t;

	kp_printf(ks, "%5lu.%06lu\n", secs, usec_rem);
	return 0;
}

static int kplib_num_cpus(ktap_state_t *ks)
{
	set_number(ks->top, num_online_cpus());
	incr_top(ks);
	return 1;
}

/* TODO: intern string firstly */
static int kplib_arch(ktap_state_t *ks)
{
	ktap_str_t *ts = kp_str_newz(ks, utsname()->machine);
	if (unlikely(!ts))
		return -1;

	set_string(ks->top, ts);
	incr_top(ks);
	return 1;
}

/* TODO: intern string firstly */
static int kplib_kernel_v(ktap_state_t *ks)
{
	ktap_str_t *ts = kp_str_newz(ks, utsname()->release);
	if (unlikely(!ts))
		return -1;

	set_string(ks->top, ts);
	incr_top(ks);
	return 1;
}

static int kplib_kernel_string(ktap_state_t *ks)
{
	unsigned long addr = kp_arg_checknumber(ks, 1);
	char str[256] = {0};
	ktap_str_t *ts;
	char *ret;

	ret = strncpy((void *)str, (const void *)addr, 256);
	(void) &ret;  /* Silence compiler warning. */
	str[255] = '\0';

	ts = kp_str_newz(ks, str);
	if (unlikely(!ts))
		return -1;

	set_string(ks->top, ts);
	incr_top(ks);
	return 1;
}

static int kplib_user_string(ktap_state_t *ks)
{
	unsigned long addr = kp_arg_checknumber(ks, 1);
	char str[256] = {0};
	ktap_str_t *ts;
	int ret;

	pagefault_disable();
	ret = __copy_from_user_inatomic((void *)str, (const void *)addr, 256);
	(void) &ret;  /* Silence compiler warning. */
	pagefault_enable();
	str[255] = '\0';

	ts = kp_str_newz(ks, str);
	if (unlikely(!ts))
		return -1;

	set_string(ks->top, ts);
	incr_top(ks);
	return 1;
}

static int kplib_stringof(ktap_state_t *ks)
{
	ktap_val_t *v = kp_arg(ks, 1);
	const ktap_str_t *ts = NULL;

	if (itype(v) == KTAP_TEVENTSTR) {
		ts = kp_event_stringify(ks);
	} else if (itype(v) == KTAP_TKIP) {
		char str[KSYM_SYMBOL_LEN];

		SPRINT_SYMBOL(str, nvalue(v));
		ts = kp_str_newz(ks, str);
	}

	if (unlikely(!ts))
		return -1;

	set_string(ks->top++, ts);
	return 1;
}

static int kplib_ipof(ktap_state_t *ks)
{
	unsigned long addr = kp_arg_checknumber(ks, 1);

	set_ip(ks->top++, addr);
	return 1;
}

static int kplib_gettimeofday_ns(ktap_state_t *ks)
{
	set_number(ks->top, gettimeofday_ns());
	incr_top(ks);

	return 1;
}

static int kplib_gettimeofday_us(ktap_state_t *ks)
{
	set_number(ks->top, gettimeofday_ns() / NSEC_PER_USEC);
	incr_top(ks);

	return 1;
}

static int kplib_gettimeofday_ms(ktap_state_t *ks)
{
	set_number(ks->top, gettimeofday_ns() / NSEC_PER_MSEC);
	incr_top(ks);

	return 1;
}

static int kplib_gettimeofday_s(ktap_state_t *ks)
{
	set_number(ks->top, gettimeofday_ns() / NSEC_PER_SEC);
	incr_top(ks);

	return 1;
}

/*
 * use gdb to get field offset of struct task_struct, for example:
 *
 * gdb vmlinux
 * (gdb)p &(((struct task_struct *)0).prio)
 */
static int kplib_curr_taskinfo(ktap_state_t *ks)
{
	int offset = kp_arg_checknumber(ks, 1);
	int fetch_bytes  = kp_arg_checkoptnumber(ks, 2, 4); /* fetch 4 bytes */

	if (offset >= sizeof(struct task_struct)) {
		set_nil(ks->top++);
		kp_error(ks, "access out of bound value of task_struct\n");
		return 1;
	}

#define RET_VALUE ((unsigned long)current + offset)

	switch (fetch_bytes) {
	case 4:
		set_number(ks->top, *(unsigned int *)RET_VALUE);
		break;
	case 8:
		set_number(ks->top, *(unsigned long *)RET_VALUE);
		break;
	default:
		kp_error(ks, "unsupported fetch bytes in curr_task_info\n");
		set_nil(ks->top);
		break;
	}

#undef RET_VALUE

	incr_top(ks);
	return 1;
}

/*
 * This built-in function mainly purpose scripts/schedule/schedtimes.kp
 */
static int kplib_in_iowait(ktap_state_t *ks)
{
	set_number(ks->top, current->in_iowait);
	incr_top(ks);

	return 1;
}

static int kplib_in_interrupt(ktap_state_t *ks)
{
	int ret = in_interrupt();

	set_number(ks->top, ret);
	incr_top(ks);
	return 1;
}

static int kplib_exit(ktap_state_t *ks)
{
	kp_vm_try_to_exit(ks);

	/* do not execute bytecode any more in this thread */
	return -1;
}

static const ktap_libfunc_t base_lib_funcs[] = {
	{"print", kplib_print},
	{"printf", kplib_printf},
	{"print_hist", kplib_print_hist},

	{"pairs", kplib_pairs},
	{"len", kplib_len},
	{"delete", kplib_delete},

	{"stack", kplib_stack},
	{"print_trace_clock", kplib_print_trace_clock},

	{"num_cpus", kplib_num_cpus},
	{"arch", kplib_arch},
	{"kernel_v", kplib_kernel_v},
	{"kernel_string", kplib_kernel_string},
	{"user_string", kplib_user_string},
	{"stringof", kplib_stringof},
	{"ipof", kplib_ipof},

	{"gettimeofday_ns", kplib_gettimeofday_ns},
	{"gettimeofday_us", kplib_gettimeofday_us},
	{"gettimeofday_ms", kplib_gettimeofday_ms},
	{"gettimeofday_s", kplib_gettimeofday_s},

	{"curr_taskinfo", kplib_curr_taskinfo},

	{"in_iowait", kplib_in_iowait},
	{"in_interrupt", kplib_in_interrupt},

	{"exit", kplib_exit},
	{NULL}
};

int kp_lib_init_base(ktap_state_t *ks)
{
	return kp_vm_register_lib(ks, NULL, base_lib_funcs); 
}
