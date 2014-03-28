/*
 * lib_timer.c - timer library support for ktap
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2014 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
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

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_vm.h"
#include "kp_events.h"

struct ktap_hrtimer {
	struct hrtimer timer;
	ktap_state_t *ks;
	ktap_func_t *fn;
	u64 ns;
	struct list_head list;
};

/*
 * Currently ktap disallow tracing event in timer callback closure,
 * that will corrupt ktap_state_t and ktap stack, because timer closure
 * and event closure use same irq percpu ktap_state_t and stack.
 * We can use a different percpu ktap_state_t and stack for timer purpuse,
 * but that's don't bring any big value with cost on memory consuming.
 *
 * So just simply disable tracing in timer closure,
 * get_recursion_context()/put_recursion_context() is used for this purpose.
 */
static enum hrtimer_restart hrtimer_ktap_fn(struct hrtimer *timer)
{
	struct ktap_hrtimer *t;
	ktap_state_t *ks;
	int rctx;

	rcu_read_lock_sched_notrace();

	t = container_of(timer, struct ktap_hrtimer, timer);
	rctx = get_recursion_context(t->ks);

	ks = kp_vm_new_thread(t->ks, rctx);
	set_func(ks->top, t->fn);
	incr_top(ks);
	kp_vm_call(ks, ks->top - 1, 0);
	kp_vm_exit_thread(ks);

	hrtimer_add_expires_ns(timer, t->ns);

	put_recursion_context(ks, rctx);
	rcu_read_unlock_sched_notrace();

	return HRTIMER_RESTART;
}

static int set_tick_timer(ktap_state_t *ks, u64 period, ktap_func_t *fn)
{
	struct ktap_hrtimer *t;

	t = kp_malloc(ks, sizeof(*t));
	if (unlikely(!t))
		return -ENOMEM;
	t->ks = ks;
	t->fn = fn;
	t->ns = period;

	INIT_LIST_HEAD(&t->list);
	list_add(&t->list, &(G(ks)->timers));

	hrtimer_init(&t->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	t->timer.function = hrtimer_ktap_fn;
	hrtimer_start(&t->timer, ns_to_ktime(period), HRTIMER_MODE_REL);

	return 0;
}

static int set_profile_timer(ktap_state_t *ks, u64 period, ktap_func_t *fn)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_SOFTWARE;
	attr.config = PERF_COUNT_SW_CPU_CLOCK;
	attr.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
			   PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD;
	attr.sample_period = period;
	attr.size = sizeof(attr);
	attr.disabled = 0;

	return kp_event_create(ks, &attr, NULL, NULL, fn);
}

static int do_tick_profile(ktap_state_t *ks, int is_tick)
{
	const char *str = kp_arg_checkstring(ks, 1);
	ktap_func_t *fn = kp_arg_checkfunction(ks, 2);
	const char *tmp;
	char interval_str[32] = {0};
	char suffix[10] = {0};
	int i = 0, ret, n;
	int factor;

	tmp = str;
	while (isdigit(*tmp))
		tmp++;

	strncpy(interval_str, str, tmp - str);
	if (kstrtoint(interval_str, 10, &n))
		goto error;

	strncpy(suffix, tmp, 9);
	while (suffix[i] != ' ' && suffix[i] != '\0')
		i++;

	suffix[i] = '\0';

	if (!strcmp(suffix, "s") || !strcmp(suffix, "sec"))
		factor = NSEC_PER_SEC;
	else if (!strcmp(suffix, "ms") || !strcmp(suffix, "msec"))
		factor = NSEC_PER_MSEC;
	else if (!strcmp(suffix, "us") || !strcmp(suffix, "usec"))
		factor = NSEC_PER_USEC;
	else
		goto error;

	if (is_tick)
		ret = set_tick_timer(ks, (u64)factor * n, fn);
	else
		ret = set_profile_timer(ks, (u64)factor * n, fn);

	return ret;

 error:
	kp_error(ks, "cannot parse timer interval: %s\n", str);
	return -1;
}

/*
 * tick-n probes fire on only one CPU per interval.
 * valid time suffixes: sec/s, msec/ms, usec/us
 */
static int kplib_timer_tick(ktap_state_t *ks)
{
	/* timer.tick cannot be called in trace_end state */
	if (G(ks)->state != KTAP_RUNNING) {
		kp_error(ks,
			 "timer.tick only can be called in RUNNING state\n");
		return -1;
	}

	return do_tick_profile(ks, 1);
}

/*
 * A profile-n probe fires every fixed interval on every CPU
 * valid time suffixes: sec/s, msec/ms, usec/us
 */
static int kplib_timer_profile(ktap_state_t *ks)
{
	/* timer.profile cannot be called in trace_end state */
	if (G(ks)->state != KTAP_RUNNING) {
		kp_error(ks,
			 "timer.profile only can be called in RUNNING state\n");
		return -1;
	}

	return do_tick_profile(ks, 0);
}

void kp_exit_timers(ktap_state_t *ks)
{
	struct ktap_hrtimer *t, *tmp;
	struct list_head *timers_list = &(G(ks)->timers);

	list_for_each_entry_safe(t, tmp, timers_list, list) {
		hrtimer_cancel(&t->timer);
		kp_free(ks, t);
	}
}

static const ktap_libfunc_t timer_lib_funcs[] = {
	{"profile",	kplib_timer_profile},
	{"tick",	kplib_timer_tick},
	{NULL}
};

int kp_lib_init_timer(ktap_state_t *ks)
{
	return kp_vm_register_lib(ks, "timer", timer_lib_funcs);
}

