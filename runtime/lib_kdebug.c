/*
 * lib_kdebug.c - kdebug library support for ktap
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

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/ftrace_event.h>
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_transport.h"
#include "kp_vm.h"
#include "kp_events.h"

/**
 * function kdebug.trace_by_id
 *
 * @uaddr: userspace address refer to ktap_eventdesc_t
 * @closure
 */
static int kplib_kdebug_trace_by_id(ktap_state_t *ks)
{
	unsigned long uaddr = kp_arg_checknumber(ks, 1);
	ktap_func_t *fn = kp_arg_checkfunction(ks, 2);
	struct task_struct *task = G(ks)->trace_task;
	ktap_eventdesc_t eventsdesc;
	char *filter = NULL;
	int *id_arr;
	int ret, i;

	if (G(ks)->mainthread != ks) {
		kp_error(ks,
		    "kdebug.trace_by_id only can be called in mainthread\n");
		return -1;
	}

	/* kdebug.trace_by_id cannot be called in trace_end state */
	if (G(ks)->state != KTAP_RUNNING) {
		kp_error(ks,
		    "kdebug.trace_by_id only can be called in RUNNING state\n");
		return -1;
	}

	/* copy ktap_eventdesc_t from userspace */
	ret = copy_from_user(&eventsdesc, (void *)uaddr,
			     sizeof(ktap_eventdesc_t));
	if (ret < 0)
		return -1;

	if (eventsdesc.filter) {
		int len;

		len = strlen_user(eventsdesc.filter);
		if (len > 0x1000)
			return -1;

		filter = kmalloc(len + 1, GFP_KERNEL);
		if (!filter)
			return -1;

		/* copy filter string from userspace */
		if (strncpy_from_user(filter, eventsdesc.filter, len) < 0) {
			kfree(filter);
			return -1;
		}
	}

	id_arr = kmalloc(eventsdesc.nr * sizeof(int), GFP_KERNEL);
	if (!id_arr) {
		kfree(filter);
		return -1;
	}

	/* copy all event id from userspace */
	ret = copy_from_user(id_arr, eventsdesc.id_arr,
			     eventsdesc.nr * sizeof(int));
	if (ret < 0) {
		kfree(filter);
		kfree(id_arr);
		return -1;
	}

	fn = clvalue(kp_arg(ks, 2));

	for (i = 0; i < eventsdesc.nr; i++) {
		struct perf_event_attr attr;

		cond_resched();

		if (signal_pending(current)) {
			flush_signals(current);
			kfree(filter);
			kfree(id_arr);
			return -1;
		}

		memset(&attr, 0, sizeof(attr));
		attr.type = PERF_TYPE_TRACEPOINT;	
		attr.config = id_arr[i];
		attr.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
				   PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD;
		attr.sample_period = 1;
		attr.size = sizeof(attr);
		attr.disabled = 0;

		/* register event one by one */
		ret = kp_event_create(ks, &attr, task, filter, fn);
		if (ret < 0)
			break;
	}

	kfree(filter);
	kfree(id_arr);
	return 0;
}

static int kplib_kdebug_trace_end(ktap_state_t *ks)
{
	/* trace_end_closure will be called when ktap main thread exit */
	G(ks)->trace_end_closure = kp_arg_checkfunction(ks, 1);
	return 0;
}

#if 0
static int kplib_kdebug_tracepoint(ktap_state_t *ks)
{
	const char *event_name = kp_arg_checkstring(ks, 1);
	ktap_func_t *fn = kp_arg_checkfunction(ks, 2);

	if (G(ks)->mainthread != ks) {
		kp_error(ks,
		    "kdebug.tracepoint only can be called in mainthread\n");
		return -1;
	}

	/* kdebug.tracepoint cannot be called in trace_end state */
	if (G(ks)->state != KTAP_RUNNING) {
		kp_error(ks,
		    "kdebug.tracepoint only can be called in RUNNING state\n");
		return -1;
	}

	return kp_event_create_tracepoint(ks, event_name, fn);
}
#endif

static int kplib_kdebug_kprobe(ktap_state_t *ks)
{
	const char *event_name = kp_arg_checkstring(ks, 1);
	ktap_func_t *fn = kp_arg_checkfunction(ks, 2);

	if (G(ks)->mainthread != ks) {
		kp_error(ks,
		    "kdebug.kprobe only can be called in mainthread\n");
		return -1;
	}

	/* kdebug.kprobe cannot be called in trace_end state */
	if (G(ks)->state != KTAP_RUNNING) {
		kp_error(ks,
		    "kdebug.kprobe only can be called in RUNNING state\n");
		return -1;
	}

	return kp_event_create_kprobe(ks, event_name, fn);
}
static const ktap_libfunc_t kdebug_lib_funcs[] = {
	{"trace_by_id", kplib_kdebug_trace_by_id},
	{"trace_end", kplib_kdebug_trace_end},

#if 0
	{"tracepoint", kplib_kdebug_tracepoint},
#endif
	{"kprobe", kplib_kdebug_kprobe},
	{NULL}
};

int kp_lib_init_kdebug(ktap_state_t *ks)
{
	return kp_vm_register_lib(ks, "kdebug", kdebug_lib_funcs);
}

