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

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include "../ktap.h"

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

	ktap_transport_write(ks, buff, len);
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

struct hrtimer_ktap {
	struct hrtimer timer;
	ktap_State *ks;
	Closure *cl;
	u64 ns;
	struct list_head list;
};

static enum hrtimer_restart hrtimer_ktap_fn(struct hrtimer *timer)
{
	ktap_State *ks;
	struct hrtimer_ktap *t;

	/* 
	 * we need to make sure timer cannot running conflict with tracing
	 * ktap_newthread use percpu ktap_State, we need to avoid timer
	 * callback closure running with tracepoint enabled, then percpu
	 * ktap_State will crash. so here make ktap_in_tracing as true, to
	 * tell ktap not running any tracepoint in timer callback closure.
	 */
	__this_cpu_write(ktap_in_tracing, true);

	t = container_of(timer, struct hrtimer_ktap, timer);

	ks = ktap_newthread(t->ks);
	setcllvalue(ks->top, t->cl);
	incr_top(ks);
	ktap_call(ks, ks->top - 1, 0);
	ktap_exitthread(ks);

	hrtimer_add_expires_ns(timer, t->ns);

	__this_cpu_write(ktap_in_tracing, false);

	return HRTIMER_RESTART;
}

static int get_ns_period(ktap_State *ks, Tvalue *o, u64 *period)
{
	const char *p, *tmp;
	char digits[32] = {0};
	unsigned long long res;
	int factor;

	if (!ttisstring(o)) {
		ktap_printf(ks, "Error: need period factor: s, ms, us, ns\n");
		return -1;
	}

	p = svalue(o);
	tmp = p;

	while (*tmp <= '9' && *tmp >= '0') {
		tmp++;
	}

	strncpy(digits, p, tmp-p);
	if (kstrtoull(digits, 10, &res)) {
		ktap_printf(ks, "Error: digit %u parse error\n", res);
		return -1;
	}

	if (!strcmp(tmp, "s")) {
		factor = NSEC_PER_SEC;
	} else if (!strcmp(tmp, "ms")) {
		factor = NSEC_PER_MSEC;
	} else if (!strcmp(tmp, "us")) {
		factor = NSEC_PER_USEC;
	} else if (!strcmp(tmp, "ns")) {
		factor = 1;
	} else {
		ktap_printf(ks, "unknown period factor %s\n", tmp);
		return -1;
	}

	*period = (u64)(res * factor);

	return 0;
}

static int ktap_lib_timer(ktap_State *ks)
{
	struct hrtimer_ktap *t;
	u64 period;
	Tvalue *tracefunc;
	Closure *cl;

	if (get_ns_period(ks, GetArg(ks, 1), &period))
		return -1;
	
	if (GetArgN(ks) >= 2) {
		tracefunc = GetArg(ks, 2);

		if (ttisfunc(tracefunc))
			cl = (Closure *)gcvalue(tracefunc);
		else
			cl = NULL;
	} else
		cl = NULL;

	t = ktap_malloc(ks, sizeof(*t));
	t->ks = ks;
	t->cl = cl;
	t->ns = period;

	INIT_LIST_HEAD(&t->list);
	list_add(&t->list, &(G(ks)->timers));

	hrtimer_init(&t->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	t->timer.function = hrtimer_ktap_fn;
	hrtimer_start(&t->timer, ns_to_ktime(period), HRTIMER_MODE_REL);

	return 0;
}

void ktap_exit_timers(ktap_State *ks)
{
	struct hrtimer_ktap *t, *tmp;
	struct list_head *timers_list = &(G(ks)->timers);
	unsigned long flags;

	/* we need disable irq when cleanup timers, for safety */
	local_irq_save(flags);

	list_for_each_entry_safe(t, tmp, timers_list, list) {
		hrtimer_cancel(&t->timer);
		ktap_free(ks, t);
	}

	local_irq_restore(flags);
}

static int ktap_lib_dummy(ktap_State *ks)
{
	return 0;
}

static const ktap_Reg oslib_funcs[] = {
	{"clock", ktap_lib_clock},
	{"info", ktap_lib_info},
	{"sleep", ktap_lib_sleep},
	{"wait", ktap_lib_wait},
	{"timer", ktap_lib_timer},
	{NULL}
};

void ktap_init_oslib(ktap_State *ks)
{
	ktap_register_lib(ks, "os", oslib_funcs);
}

