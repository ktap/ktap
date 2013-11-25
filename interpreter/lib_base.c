/*
 * baselib.c - ktapvm kernel module base library
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
#include "kp_vm.h"

static int ktap_lib_next(ktap_state *ks)
{
	ktap_table *t = hvalue(ks->top - 2);

	if (kp_table_next(ks, t, ks->top-1)) {
		ks->top += 1;
		return 2;
	} else {
		ks->top -= 1;
		set_nil(ks->top++);
		return 1;
	}
}

static int ktap_lib_pairs(ktap_state *ks)
{
	ktap_value *v = kp_arg(ks, 1);
	ktap_table *t;

	if (is_table(v)) {
		t = hvalue(v);
	} else if (is_ptable(v)) {
		t = kp_ptable_synthesis(ks, phvalue(v));
	} else if (is_nil(v)) {
		kp_error(ks, "table is nil in pairs\n");
		return 0;
	} else {
		kp_error(ks, "wrong argument for pairs\n");
		return 0;
	}

	set_cfunction(ks->top++, ktap_lib_next);
	set_table(ks->top++, t);
	set_nil(ks->top++);
	return 3;
}

static int ktap_lib_sort_next(ktap_state *ks)
{
	ktap_table *t = hvalue(ks->top - 2);

	if (kp_table_sort_next(ks, t, ks->top-1)) {
		ks->top += 1;
		return 2;
	} else {
		ks->top -= 1;
		set_nil(ks->top++);
		return 1;
	}
}

static int ktap_lib_sort_pairs(ktap_state *ks)
{
	ktap_value *v = kp_arg(ks, 1);
	ktap_closure *cmp_func = NULL;
	ktap_table *t;

	if (is_table(v)) {
		t = hvalue(v);
	} else if (is_ptable(v)) {
		t = kp_ptable_synthesis(ks, phvalue(v));
	} else if (is_nil(v)) {
		kp_error(ks, "table is nil in pairs\n");
		return 0;
	} else {
		kp_error(ks, "wrong argument for pairs\n");
		return 0;
	}

	if (kp_arg_nr(ks) > 1) {
		kp_arg_check(ks, 2, KTAP_TFUNCTION);
		cmp_func = clvalue(kp_arg(ks, 2));
	}

	kp_table_sort(ks, t, cmp_func); 
	set_cfunction(ks->top++, ktap_lib_sort_next);
	set_table(ks->top++, t);
	set_nil(ks->top++);
	return 3;
}

static int ktap_lib_len(ktap_state *ks)
{
	int len = kp_objlen(ks, kp_arg(ks, 1));

	if (len < 0)
		return -1;

	set_number(ks->top, len);
	incr_top(ks);
	return 1;
}

static int ktap_lib_print(ktap_state *ks)
{
	int i;
	int n = kp_arg_nr(ks);

	for (i = 1; i <= n; i++) {
		ktap_value *arg = kp_arg(ks, i);
		if (i > 1)
			kp_puts(ks, "\t");
		kp_showobj(ks, arg);
	}

	kp_puts(ks, "\n");

	return 0;
}

/* don't engage with tstring when printf, use buffer directly */
static int ktap_lib_printf(ktap_state *ks)
{
	struct trace_seq *seq;

	preempt_disable_notrace();

	seq = kp_percpu_data(ks, KTAP_PERCPU_DATA_BUFFER);
	trace_seq_init(seq);

	if (kp_str_fmt(ks, seq))
		goto out;

	seq->buffer[seq->len] = '\0';
	kp_transport_write(ks, seq->buffer, seq->len + 1);

 out:
	preempt_enable_notrace();
	return 0;
}

#ifdef CONFIG_STACKTRACE
static int ktap_lib_print_backtrace(ktap_state *ks)
{
	int skip = 10, max_entries = 10;
	int n = kp_arg_nr(ks);

	if (n >= 1) {
		kp_arg_check(ks, 1, KTAP_TNUMBER);
		skip = nvalue(kp_arg(ks, 1));
	}
	if (n >= 2) {
		kp_arg_check(ks, 2, KTAP_TNUMBER);
		max_entries = nvalue(kp_arg(ks, 2));
		max_entries = min(max_entries, KTAP_MAX_STACK_ENTRIES);
	}

	kp_transport_print_backtrace(ks, skip, max_entries);
	return 0;
}
#else
static int ktap_lib_print_backtrace(ktap_state *ks)
{
	kp_error(ks, "Please enable CONFIG_STACKTRACE before use "
		     "ktap print_backtrace\n");
	return 0;
}
#endif

static int ktap_lib_backtrace(ktap_state *ks)
{
	struct stack_trace trace;
	int skip = 10, max_entries = 10;
	int n = kp_arg_nr(ks);
	ktap_btrace *bt;

	if (n >= 1) {
		kp_arg_check(ks, 1, KTAP_TNUMBER);
		skip = nvalue(kp_arg(ks, 1));
	}
	if (n >= 2) {
		kp_arg_check(ks, 2, KTAP_TNUMBER);
		max_entries = nvalue(kp_arg(ks, 2));
		max_entries = min(max_entries, KTAP_MAX_STACK_ENTRIES);
	}

	bt = kp_percpu_data(ks, KTAP_PERCPU_DATA_BTRACE);

	trace.nr_entries = 0;
	trace.skip = skip;
	trace.max_entries = max_entries;
	trace.entries = (unsigned long *)(bt + 1);
	save_stack_trace(&trace);

	bt->nr_entries = trace.nr_entries;
	set_btrace(ks->top, bt);
	incr_top(ks);
	return 1;
}

extern unsigned long long ns2usecs(cycle_t nsec);
static int ktap_lib_print_trace_clock(ktap_state *ks)
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

static int ktap_lib_exit(ktap_state *ks)
{
	kp_exit(ks);

	/* do not execute bytecode any more in this thread */
	return -1;
}

static int ktap_lib_pid(ktap_state *ks)
{
	set_number(ks->top, (int)current->pid);
	incr_top(ks);
	return 1;
}

static int ktap_lib_tid(ktap_state *ks)
{
	pid_t pid = task_pid_vnr(current);

	set_number(ks->top, (int)pid);
	incr_top(ks);
	return 1;
}

static int ktap_lib_uid(ktap_state *ks)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
	uid_t uid = from_kuid_munged(current_user_ns(), current_uid());
#else
	uid_t uid = current_uid();
#endif
	set_number(ks->top, (int)uid);
	incr_top(ks);
	return 1;
}

static int ktap_lib_execname(ktap_state *ks)
{
	ktap_string *ts = kp_tstring_new(ks, current->comm);
	set_string(ks->top, ts);
	incr_top(ks);
	return 1;
}

static int ktap_lib_cpu(ktap_state *ks)
{
	set_number(ks->top, smp_processor_id());
	incr_top(ks);
	return 1;
}

static int ktap_lib_num_cpus(ktap_state *ks)
{
	set_number(ks->top, num_online_cpus());
	incr_top(ks);
	return 1;
}

static int ktap_lib_in_interrupt(ktap_state *ks)
{
	int ret = in_interrupt();

	set_number(ks->top, ret);
	incr_top(ks);
	return 1;
}

static int ktap_lib_arch(ktap_state *ks)
{
	set_string(ks->top, kp_tstring_new(ks, utsname()->machine));
	incr_top(ks);
	return 1;
}

static int ktap_lib_kernel_v(ktap_state *ks)
{
	set_string(ks->top, kp_tstring_new(ks, utsname()->release));
	incr_top(ks);
	return 1;
}

static int ktap_lib_kernel_string(ktap_state *ks)
{
	unsigned long addr;
	char str[256] = {0};
	char *ret;

	kp_arg_check(ks, 1, KTAP_TNUMBER);

	addr = nvalue(kp_arg(ks, 1));

	ret = strncpy((void *)str, (const void *)addr, 256);
	(void) &ret;  /* Silence compiler warning. */

	str[255] = '\0';
	set_string(ks->top, kp_tstring_new_local(ks, str));

	incr_top(ks);
	return 1;
}

static int ktap_lib_user_string(ktap_state *ks)
{
	unsigned long addr;
	char str[256] = {0};
	int ret;

	kp_arg_check(ks, 1, KTAP_TNUMBER);

	addr = nvalue(kp_arg(ks, 1));

	pagefault_disable();
	ret = __copy_from_user_inatomic((void *)str, (const void *)addr, 256);
	(void) &ret;  /* Silence compiler warning. */
	pagefault_enable();
	str[255] = '\0';
	set_string(ks->top, kp_tstring_new(ks, str));

	incr_top(ks);
	return 1;
}

static int ktap_lib_histogram(ktap_state *ks)
{
	ktap_value *v = kp_arg(ks, 1);

	if (is_table(v))
		kp_table_histogram(ks, hvalue(v));
	else if (is_ptable(v))
		kp_ptable_histogram(ks, phvalue(v));

	return 0;
}

static int ktap_lib_ptable(ktap_state *ks)
{
	ktap_ptable *ph;

	ph = kp_ptable_new(ks);
	set_ptable(ks->top, ph);
	incr_top(ks);
	return 1;
}

static int ktap_lib_count(ktap_state *ks)
{
	ktap_value *v = kp_arg(ks, 1);
	ktap_stat_data *sd;

	if (is_nil(v)) {
		set_number(ks->top, 0);
		incr_top(ks);
		return 1;	
	}

	kp_arg_check(ks, 1, KTAP_TSTATDATA);
	sd = sdvalue(v);

	set_number(ks->top, sd->count);
	incr_top(ks);
	return 1;
}

static int ktap_lib_max(ktap_state *ks)
{
	ktap_value *v = kp_arg(ks, 1);
	ktap_stat_data *sd;

	if (is_nil(v)) {
		set_number(ks->top, 0);
		incr_top(ks);
		return 1;	
	}

	kp_arg_check(ks, 1, KTAP_TSTATDATA);
	sd = sdvalue(v);

	set_number(ks->top, sd->max);
	incr_top(ks);
	return 1;
}

static int ktap_lib_min(ktap_state *ks)
{
	ktap_value *v = kp_arg(ks, 1);
	ktap_stat_data *sd;

	if (is_nil(v)) {
		set_number(ks->top, 0);
		incr_top(ks);
		return 1;	
	}

	kp_arg_check(ks, 1, KTAP_TSTATDATA);
	sd = sdvalue(v);

	set_number(ks->top, sd->min);
	incr_top(ks);
	return 1;
}

static int ktap_lib_sum(ktap_state *ks)
{
	ktap_value *v = kp_arg(ks, 1);
	ktap_stat_data *sd;

	if (is_nil(v)) {
		set_number(ks->top, 0);
		incr_top(ks);
		return 1;	
	}

	kp_arg_check(ks, 1, KTAP_TSTATDATA);
	sd = sdvalue(v);

	set_number(ks->top, sd->sum);
	incr_top(ks);
	return 1;
}

static int ktap_lib_avg(ktap_state *ks)
{
	ktap_value *v = kp_arg(ks, 1);
	ktap_stat_data *sd;

	if (is_nil(v)) {
		set_number(ks->top, 0);
		incr_top(ks);
		return 1;	
	}

	kp_arg_check(ks, 1, KTAP_TSTATDATA);
	sd = sdvalue(v);

	set_number(ks->top, sd->sum / sd->count);
	incr_top(ks);
	return 1;
}

static int ktap_lib_delete(ktap_state *ks)
{
	kp_arg_check(ks, 1, KTAP_TTABLE);

	kp_table_clear(ks, hvalue(kp_arg(ks, 1)));
	return 0;
}

static int ktap_lib_gettimeofday_us(ktap_state *ks)
{
	set_number(ks->top, gettimeofday_us());
	incr_top(ks);

	return 1;
}

/*
 * use gdb to get field offset of struct task_struct, for example:
 *
 * gdb vmlinux
 * (gdb)p &(((struct task_struct *)0).prio)
 */
static int ktap_lib_curr_task_info(ktap_state *ks)
{
	int offset;
	int fetch_bytes;

	kp_arg_check(ks, 1, KTAP_TNUMBER);

	offset = nvalue(kp_arg(ks, 1));
	
	if (kp_arg_nr(ks) == 1)
		fetch_bytes = 4; /* default fetch 4 bytes*/
	else {
		kp_arg_check(ks, 2, KTAP_TNUMBER);
		fetch_bytes = nvalue(kp_arg(ks, 2));
	}

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
static int ktap_lib_in_iowait(ktap_state *ks)
{
	set_number(ks->top, current->in_iowait);
	incr_top(ks);

	return 1;
}

static const ktap_Reg base_funcs[] = {
	{"pairs", ktap_lib_pairs},
	{"sort_pairs", ktap_lib_sort_pairs},
	{"len", ktap_lib_len},
	{"print", ktap_lib_print},
	{"printf", ktap_lib_printf},
	{"print_backtrace", ktap_lib_print_backtrace},
	{"backtrace", ktap_lib_backtrace},
	{"print_trace_clock", ktap_lib_print_trace_clock},
	{"in_interrupt", ktap_lib_in_interrupt},
	{"exit", ktap_lib_exit},
	{"pid", ktap_lib_pid},
	{"tid", ktap_lib_tid},
	{"uid", ktap_lib_uid},
	{"execname", ktap_lib_execname},
	{"cpu", ktap_lib_cpu},
	{"num_cpus", ktap_lib_num_cpus},
	{"arch", ktap_lib_arch},
	{"kernel_v", ktap_lib_kernel_v},
	{"kernel_string", ktap_lib_kernel_string},
	{"user_string", ktap_lib_user_string},
	{"histogram", ktap_lib_histogram},
	{"ptable", ktap_lib_ptable},
	{"count", ktap_lib_count},
	{"max", ktap_lib_max},
	{"min", ktap_lib_min},
	{"sum", ktap_lib_sum},
	{"avg", ktap_lib_avg},

	{"delete", ktap_lib_delete},
	{"gettimeofday_us", ktap_lib_gettimeofday_us},
	{"curr_taskinfo", ktap_lib_curr_task_info},
	{"in_iowait", ktap_lib_in_iowait},
	{NULL}
};

void kp_init_baselib(ktap_state *ks)
{
	kp_register_lib(ks, NULL, base_funcs); 
}
