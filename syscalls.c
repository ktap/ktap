/*
 * syscalls.c - ktap syscall tracing support
 *
 * Copyright (C) 2012 Jovi Zhang <bookjovi@gmail.com>
 *
 * Author: Jovi Zhang <bookjovi@gmail.com>
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
#include <linux/kallsyms.h>
#include <trace/events/syscalls.h>
#include <trace/syscall.h>
#include "ktap.h"

static struct syscall_metadata **syscalls_metadata;

static DEFINE_MUTEX(syscall_trace_lock);
static int sys_refcount_enter;
static int sys_refcount_exit;
static DECLARE_BITMAP(enabled_enter_syscalls, NR_syscalls);
static DECLARE_BITMAP(enabled_exit_syscalls, NR_syscalls);

static struct syscall_metadata *syscall_nr_to_meta(int nr)
{
	if (!syscalls_metadata || nr >= NR_syscalls || nr < 0)
		return NULL;

	return syscalls_metadata[nr];
}

/*
 * we are not passing syscall name to trace function for performance
 * reason, try to use ktap syscall table if you want get syscall name
 */
static void call_user_closure(ktap_State *mainthread, Closure *cl,
			      int syscall_nr, int enter, int retval,
			      int argnum, void *args_data)
{
	ktap_State *ks;
	int numparames = min(argnum + 4, (int)cl->l.p->numparams);
	Tvalue *func;
	int i;

	ks = ktap_newthread(mainthread);
	setcllvalue(ks->top, cl);
	func = ks->top;
	incr_top(ks);

	//ktap_printf(ks, "argnum: %d, numpar: %d\n", argnum, numparames);

	if (numparames > 0) {
		setnvalue(ks->top, syscall_nr);
		incr_top(ks);
		numparames--;
	}

	if (numparames > 0) {
		setnvalue(ks->top, enter);
		incr_top(ks);
		numparames--;
	}

	if (numparames > 0) {
		setnvalue(ks->top, retval);
		incr_top(ks);
		numparames--;
	}

	if (numparames > 0) {
		setnvalue(ks->top, argnum);
		incr_top(ks);
		numparames--;
	}

	for (i = 0; (i < argnum) && (numparames > 0); i++) {
		setnvalue(ks->top, *((ktap_Number *)args_data + i));
		incr_top(ks);
		numparames--;
	}

	ktap_call(ks, func, 0);
	ktap_exitthread(ks);
}

static void syscall_raw_trace(void *data, struct pt_regs *regs, int enter)
{
	ktap_Callback_data *cbdata;
	struct hlist_node *pos;
	struct syscall_metadata *sys_data;
	struct ftrace_event_call *call;
	unsigned long args_data[7];
	int syscall_nr, syscall_ret = 0;
	int i;

	syscall_nr = syscall_get_nr(current, regs);
	if (syscall_nr < 0)
		return;

	if (enter) {
		if (!test_bit(syscall_nr, enabled_enter_syscalls))
			return;
	} else {
		if (!test_bit(syscall_nr, enabled_exit_syscalls))
			return;
	}

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	call = enter ? sys_data->enter_event : sys_data->exit_event;

	syscall_get_arguments(current, regs, 0, sys_data->nb_args,
			      (unsigned long *)&args_data);

	if (!enter)
		syscall_ret = syscall_get_return_value(current, regs);

	/* todo: moew for loop out of this for_each_entry */
	hlist_for_each_entry_rcu(cbdata, pos,
				 &call->ktap_callback_list, node) {
		ktap_State *ks = cbdata->ks;
		Closure *cl = cbdata->cl;

		/* call user defined trace function */
		if (cl) {
			call_user_closure(ks, cl, syscall_nr, enter,
					  syscall_ret,
					  enter ? sys_data->nb_args : 0,
					  &args_data[0]);
			continue;
		}

		/* default print */
		if (!enter) {
			ktap_printf(ks, "%d[%s]\t%s return %d\n", current->pid,
					current->comm, sys_data->name,
					syscall_ret);
			continue;
		}

		ktap_printf(ks, "%d[%s]\t%s(", current->pid,
				current->comm, sys_data->name);

		for (i = 0; i < sys_data->nb_args; i++) {
			if (i == sys_data->nb_args - 1) {
				ktap_printf(ks, "(%s)%s: 0x%x)\n", sys_data->types[i],
						sys_data->args[i], args_data[i]);
				break;
			}
			ktap_printf(ks, "(%s)%s: 0x%x, ", sys_data->types[i],
					sys_data->args[i], args_data[i]);
		}
	}
}

static void trace_syscall_enter(void *data, struct pt_regs *regs, long id)
{
	syscall_raw_trace(data, regs, 1);
}

static void trace_syscall_exit(void *data, struct pt_regs *regs, long id)
{
	syscall_raw_trace(data, regs, 0);
}

static int find_syscall_nr(const char *name)
{
	int i;

	for (i = 0; i < NR_syscalls; i++) {
		if (!syscalls_metadata[i])
			continue;
		if (!strcmp(syscalls_metadata[i]->name + 4, name))
			return i;
	}
	return -1;
}

static int get_syscall_info(struct ftrace_event_call *call, int *enter)
{
	char syscall_name[16] = {0};
	int syscall_nr;

	if (!strncmp(call->name, "sys_enter_", 10)) {
		*enter = 1;
		strncpy(syscall_name, call->name + 10, 16);
	} else if (!strncmp(call->name, "sys_exit_", 9)) {
		*enter = 0;
		strncpy(syscall_name, call->name + 9, 16);
	} else
		return -EINVAL;

	syscall_nr = find_syscall_nr(syscall_name);
	if (syscall_nr < 0)
		return -EINVAL;

	return syscall_nr;
}

int start_trace_syscalls(struct ftrace_event_call *call,
			  ktap_Callback_data *callback)
{
	int enter, syscall_nr, ret = 0;

	syscall_nr = get_syscall_info(call, &enter);
	if (syscall_nr < 0)
		return syscall_nr;

	mutex_lock(&syscall_trace_lock);
	if (enter) {
		if (!sys_refcount_enter)
			ret = register_trace_sys_enter(trace_syscall_enter, NULL);
		if (!ret) {
			set_bit(syscall_nr, enabled_enter_syscalls);
			sys_refcount_enter++;
		}
	} else {
		if (!sys_refcount_exit)
			ret = register_trace_sys_exit(trace_syscall_exit, NULL);
		if (!ret) {
			set_bit(syscall_nr, enabled_exit_syscalls);
			sys_refcount_exit++;
		}
	}
	mutex_unlock(&syscall_trace_lock);

	return 0;
}

void stop_trace_syscalls(struct ftrace_event_call *call,
			 ktap_Callback_data *callback)
{
	int enter, syscall_nr;

	syscall_nr = get_syscall_info(call, &enter);
	if (syscall_nr < 0)
		return;

	mutex_lock(&syscall_trace_lock);
	if (enter) {
		sys_refcount_enter--;
		clear_bit(syscall_nr, enabled_enter_syscalls);
		if (!sys_refcount_enter)
			unregister_trace_sys_enter(trace_syscall_enter, NULL);
	} else {
		sys_refcount_exit--;
		clear_bit(syscall_nr, enabled_exit_syscalls);
		if (!sys_refcount_exit)
			unregister_trace_sys_exit(trace_syscall_exit, NULL);
	}
	mutex_unlock(&syscall_trace_lock);

	tracepoint_synchronize_unregister();
}

void ktap_init_syscalls(ktap_State *ks)
{
	unsigned long addr;
	Table *syscall_tbl;
	int i;

	/* this is hacking, syscalls_metadata is already inited in ftrace */
	addr = kallsyms_lookup_name("syscalls_metadata");
	if (!addr) {
		ktap_printf(ks, "Error: Cannot get syscalls_metadata\n");
		return;
	}

	syscalls_metadata = (struct syscall_metadata **)(*(unsigned long *)addr);

	/*
	 * init basic syscall ktap table
	 * syscall_talbe[syscall_nr]   = syscall_name
	 * syscall_talbe[syscall_name] = syscall_nr
	 */
	syscall_tbl = table_new(ks);
	table_resize(ks, syscall_tbl, NR_syscalls, NR_syscalls);

	for (i = 0; i < NR_syscalls; i++) {
		Tvalue vn, vi;

		if (!syscalls_metadata[i])
			continue;

		setsvalue(&vn, tstring_new(ks, syscalls_metadata[i]->name));
        	table_setint(ks, syscall_tbl, i, &vn);

		setnvalue(&vi, i);
        	table_setvalue(ks, syscall_tbl, &vn, &vi);
	}
}

