/*
 * baselib.c - ktapvm kernel module base library
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

#include <linux/hardirq.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/time.h>
#include "../../include/ktap.h"

/* get argument operation macro */
#define GetArg(ks, n)	((ks)->ci->func + (n))
#define GetArgN(ks)	((int)(ks->top - (ks->ci->func + 1)))

static int ktap_lib_next(ktap_State *ks)
{
	Table *t = hvalue(ks->top - 2);

	if (kp_table_next(ks, t, ks->top)) {
		ks->top += 2;
		return 2;
	} else {
		setnilvalue(ks->top++);
		return 1;
	}
}

static int ktap_lib_pairs(ktap_State *ks)
{
	Table *t = hvalue(GetArg(ks, 1));

	setfvalue(ks->top++, ktap_lib_next);
	sethvalue(ks->top++, t);
	setnilvalue(ks->top++);
	return 3;
}

static int ktap_lib_len(ktap_State *ks)
{
	int len = kp_objlen(ks, GetArg(ks, 1));

	if (len < 0)
		return -1;

	setnvalue(ks->top, len);
	incr_top(ks);
	return 1;
}

static int ktap_lib_print(ktap_State *ks)
{
	int i;
	int n = GetArgN(ks);

	for (i = 1; i <= n; i++) {
		Tvalue *arg = GetArg(ks, i);
		if (i > 1)
			kp_printf(ks, "\t");
		kp_showobj(ks, arg);
	}

	kp_printf(ks, "\n");

	return 0;
}

static struct trace_seq mainthread_printf_seq;
static DEFINE_PER_CPU(struct trace_seq, printf_seq);

/* don't engage with tstring when printf, use buffer directly */
static int ktap_lib_printf(ktap_State *ks)
{
	struct trace_seq *seq;

	if (ks == G(ks)->mainthread) {
		seq = &mainthread_printf_seq;		
	} else {
		seq = &per_cpu(printf_seq, smp_processor_id());
	}

	trace_seq_init(seq);
	if (kp_strfmt(ks, seq)) {
		return 0;
	}

	seq->buffer[seq->len] = '\0';
	kp_transport_write(ks, seq->buffer, seq->len);

	return 0;
}

static int ktap_lib_exit(ktap_State *ks)
{
	kp_exit(ks);
	return 0;
}

static int ktap_lib_pid(ktap_State *ks)
{
	pid_t pid = task_tgid_vnr(current);

	setnvalue(ks->top, (int)pid);
	incr_top(ks);
	return 1;
}

static int ktap_lib_execname(ktap_State *ks)
{
	Tstring *ts = kp_tstring_new(ks, current->comm);
	setsvalue(ks->top, ts);
	incr_top(ks);
	return 1;
}

static int ktap_lib_cpu(ktap_State *ks)
{
	setnvalue(ks->top, smp_processor_id());
	incr_top(ks);
	return 1;
}

static int ktap_lib_num_cpus(ktap_State *ks)
{
	setnvalue(ks->top, num_online_cpus());
	incr_top(ks);
	return 1;
}

static int ktap_lib_in_interrupt(ktap_State *ks)
{
	int ret = in_interrupt();

	setnvalue(ks->top, ret);
	incr_top(ks);
	return 1;
}

static int ktap_lib_arch(ktap_State *ks)
{
	setsvalue(ks->top, kp_tstring_new(ks, utsname()->machine));
	incr_top(ks);
	return 1;
}

static int ktap_lib_kernel_v(ktap_State *ks)
{
	setsvalue(ks->top, kp_tstring_new(ks, utsname()->release));
	incr_top(ks);
	return 1;
}

static int ktap_lib_user_string(ktap_State *ks)
{
	unsigned long addr = nvalue(GetArg(ks, 1));
	char str[256] = {0};
	int ret;

	pagefault_disable();
	ret = __copy_from_user_inatomic((void *)str, (const void *)addr, 256);
	(void) &ret;  /* Silence compiler warning. */
	pagefault_enable();
	str[255] = '\0';
	setsvalue(ks->top, kp_tstring_new(ks, str));

	incr_top(ks);
	return 1;
}

static int ktap_lib_count(ktap_State *ks)
{
	Table *tbl = hvalue(GetArg(ks, 1));
	Tvalue *k = GetArg(ks, 2);
	int n;
	Tvalue *v;

	if (GetArgN(ks) > 2)
		n = nvalue(GetArg(ks, 3));
	else
		n = 1;

	v = kp_table_set(ks, tbl, k);
	if (unlikely(isnil(v))) {
		setnvalue(v, 1);
	} else
		setnvalue(v, nvalue(v) + n);

	return 0;
}

static int ktap_lib_histogram(ktap_State *ks)
{
	kp_table_histogram(ks, hvalue(GetArg(ks, 1))); /* need to check firstly */
	return 0;
}

static int ktap_lib_gettimeofday_us(ktap_State *ks)
{
	struct timeval tv;

	do_gettimeofday(&tv);

	setnvalue(ks->top, tv.tv_sec * USEC_PER_SEC + tv.tv_usec);
	incr_top(ks);

	return 1;
}


static const ktap_Reg base_funcs[] = {
//	{"assert", ktap_assert},
//	{"collectgarbage", ktap_collectgarbage},
//	{"error", ktap_error},
//	{"getmetatable", ktap_getmetatable},
//	{"setmetatable", ktap_setmetatable},
//	{"ipairs", ktap_ipairs},
//	{"next", ktap_next},
	{"pairs", ktap_lib_pairs},
//	{"tonumber", ktap_tonumber},
//	{"tostring", ktap_tostring},
//	{"type", ktap_type},
	{"len", ktap_lib_len},
	{"print", ktap_lib_print},
	{"printf", ktap_lib_printf},
	{"in_interrupt", ktap_lib_in_interrupt},
	{"exit", ktap_lib_exit},
	{"pid", ktap_lib_pid},
	{"execname", ktap_lib_execname},
	{"cpu", ktap_lib_cpu},
	{"num_cpus", ktap_lib_num_cpus},
	{"arch", ktap_lib_arch},
	{"kernel_v", ktap_lib_kernel_v},
	{"user_string", ktap_lib_user_string},
	{"count", ktap_lib_count},
	{"histogram", ktap_lib_histogram},
	{"gettimeofday_us", ktap_lib_gettimeofday_us},
	{NULL}
};

void kp_init_baselib(ktap_State *ks)
{
	kp_register_lib(ks, NULL, base_funcs); 
}
