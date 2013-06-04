/*
 * timer.c - Linux basic library function support for ktap
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
#include "../../include/ktap.h"

/* get argument operation macro */
#define GetArg(ks, n)	((ks)->ci->func + (n))
#define GetArgN(ks)	((int)(ks->top - (ks->ci->func + 1)))

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

	ks = kp_newthread(t->ks);
	setcllvalue(ks->top, t->cl);
	incr_top(ks);
	kp_call(ks, ks->top - 1, 0);
	kp_exitthread(ks);

	hrtimer_add_expires_ns(timer, t->ns);

	__this_cpu_write(ktap_in_tracing, false);

	return HRTIMER_RESTART;
}

static int set_timer(ktap_State *ks, int factor)
{
	struct hrtimer_ktap *t;
	u64 period;
	Tvalue *tracefunc;
	int n = nvalue(GetArg(ks, 1));
	Closure *cl;

	period = (u64)factor * n;
	
	if (GetArgN(ks) >= 2) {
		tracefunc = GetArg(ks, 2);

		if (ttisfunc(tracefunc))
			cl = (Closure *)gcvalue(tracefunc);
		else
			cl = NULL;
	} else
		cl = NULL;

	if (!cl)
		return -1;

	t = kp_malloc(ks, sizeof(*t));
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

static int ktap_lib_second(ktap_State *ks)
{
	set_timer(ks, NSEC_PER_SEC);
	return 0;
}

static int ktap_lib_msecond(ktap_State *ks)
{
	set_timer(ks, NSEC_PER_MSEC);
	return 0;
}

static int ktap_lib_usecond(ktap_State *ks)
{
	set_timer(ks, NSEC_PER_USEC);
	return 0;
}

static int ktap_lib_nsecond(ktap_State *ks)
{
	set_timer(ks, 1);
	return 0;
}

static int ktap_lib_profile(ktap_State *ks)
{
	return 0;
}

void kp_exit_timers(ktap_State *ks)
{
	struct hrtimer_ktap *t, *tmp;
	struct list_head *timers_list = &(G(ks)->timers);
	unsigned long flags;

	/* we need disable irq when cleanup timers, for safety */
	local_irq_save(flags);

	list_for_each_entry_safe(t, tmp, timers_list, list) {
		hrtimer_cancel(&t->timer);
		kp_free(ks, t);
	}

	local_irq_restore(flags);
}

static const ktap_Reg timerlib_funcs[] = {
	{"s",		ktap_lib_second},
	{"sec", 	ktap_lib_second},
	{"ms",		ktap_lib_msecond},
	{"msec",	ktap_lib_msecond},
	{"us",		ktap_lib_usecond},
	{"usec",	ktap_lib_usecond},
	{"ns",		ktap_lib_nsecond},
	{"nsec",	ktap_lib_nsecond},
	{"profile",	ktap_lib_profile},
	{NULL}
};

void kp_init_timerlib(ktap_State *ks)
{
	kp_register_lib(ks, "timer", timerlib_funcs);
}

