/*
 * oslib.c - Linux basic library function support for ktap
 *
 * Copyright (C) 2012-2013 Jovi Zhang
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
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <asm-generic/uaccess.h>
#include <linux/sched.h>

#include "ktap.h"

/*
 * todo: make more robust for this function
 * make this buffer percpu
 *
 * Note:
 * this function cannot be use in net console like ssh env
 * otherwise the result is not correct, and possible crash
 * you terminal
 * Use this function in raw pts terminal
 */
void ktap_printf(ktap_State *ks, const char *fmt, ...)
{
	char buff[512];
	va_list args;
	int len;

	va_start(args, fmt);
	len = vscnprintf(buff, 512, fmt, args);
	va_end(args);

	ktap_transport_write(buff, len);
}


/* get argument operation macro */
#define GetArg(ks, n)	((ks)->ci->func + (n))
#define GetArgN(ks)	((int)(ks->top - (ks->ci->func + 1)))

static int ktap_lib_clock(ktap_State *ks)
{
	ktap_printf(ks, "ktap_clock\n");
	return 0;
}

static int ktap_lib_info(ktap_State *ks)
{
	return 0;
}

static int ktap_lib_dumpstack(ktap_State *ks)
{
	/*
	 * dump_stack implementation is arch-dependent,
	 * so it's hard to re-implement it uniformly in ktap,
	 * not easy to redirect by ktap_printf.
	 */   
	dump_stack();
	return 0;
}

static int ktap_lib_sleep(ktap_State *ks)
{
	Tvalue *time = GetArg(ks, 1);

	/* only mainthread can sleep
	if (ks != G(ks)->mainthread)
		return 0;
	*/

	msleep_interruptible(nvalue(time));

	if (fatal_signal_pending(current))
		ktap_exit(ks);
	return 0;
}

/* wait forever unit interrupt by signal */
static int ktap_lib_wait(ktap_State *ks)
{
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();
	if (fatal_signal_pending(current))
		ktap_exit(ks);

	return 0;
}

static int ktap_lib_timer(ktap_State *ks)
{


	return 0;
}

static int ktap_lib_trace(ktap_State *ks)
{
	Tvalue *evname = GetArg(ks, 1);
	char *event_name;
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

	event_name = svalue(evname);
	start_trace(ks, event_name, cl);

	return 0;
}

static int ktap_lib_trace_end(ktap_State *ks)
{
	Tvalue *endfunc;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule();
	if (fatal_signal_pending(current))
		flush_signals(current);

	end_all_trace(ks);

	if (GetArgN(ks) == 0)
		return 0;

	endfunc = GetArg(ks, 1);

	setcllvalue(ks->top, clvalue(endfunc));
	incr_top(ks);
	
	ktap_call(ks, ks->top - 1, 0);
	return 0;
}

static int ktap_lib_kprobe(ktap_State *ks)
{
	return 0;
}

static int ktap_lib_uprobe(ktap_State *ks)
{
	return 0;
}

static int ktap_lib_comm(ktap_State *ks)
{
	Tstring *ts = tstring_new(ks, current->comm);
	setsvalue(ks->top, ts);
	incr_top(ks);
	return 1;
}

static int ktap_lib_pid(ktap_State *ks)
{
	setnvalue(ks->top, task_tgid_vnr(current));
	incr_top(ks);
	return 1;
}

static int ktap_lib_dummy(ktap_State *ks)
{
	return 0;
}

static int ktap_lib_smp_processor_id(ktap_State *ks)
{
	setnvalue(ks->top, smp_processor_id());
	incr_top(ks);
	return 1;
}


static const ktap_Reg oslib_funcs[] = {
	{"clock", ktap_lib_clock},
	{"info", ktap_lib_info},
	{"smp_processor_id", ktap_lib_smp_processor_id},

	{"dumpstack", ktap_lib_dumpstack},
	{"sleep", ktap_lib_sleep},
	{"wait", ktap_lib_wait},
	{"timer_start", ktap_lib_dummy},
	{"timer_end", ktap_lib_dummy},
	{"trace", ktap_lib_trace},
	{"trace_end", ktap_lib_trace_end},
	{"kprobe", ktap_lib_kprobe},
	{"uprobe", ktap_lib_uprobe},
	{NULL}
};

static const ktap_Reg processlib_funcs[] = {
	{"comm", ktap_lib_comm},
	{"pid", ktap_lib_pid},
	{NULL}
};

static const ktap_Reg systemlib_funcs[] = {
	{"info", ktap_lib_dummy},
	{NULL}
};

void ktap_init_oslib(ktap_State *ks)
{
	ktap_register_lib(ks, "os", oslib_funcs);
	ktap_register_lib(ks, "process", processlib_funcs);
	ktap_register_lib(ks, "system", oslib_funcs);
}

