/*
 * syscalls.c - ktap syscall tracing support
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
#include <trace/events/syscalls.h>
#include <trace/syscall.h>

#include "../../trace.h"
#include "../ktap.h"

static DEFINE_MUTEX(syscall_trace_lock);
static int sys_refcount_enter;
static int sys_refcount_exit;
static DECLARE_BITMAP(enabled_enter_syscalls, NR_syscalls);
static DECLARE_BITMAP(enabled_exit_syscalls, NR_syscalls);

static void syscall_raw_trace(void *data, struct pt_regs *regs, int enter)
{
	struct syscall_metadata *sys_data;
	struct ftrace_event_call *call;
	unsigned long flags;
	int syscall_nr;

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

	if (enter) {
		struct syscall_trace_enter *entry;
		int entry_size;

		call = sys_data->enter_event;
		entry_size = sizeof(*entry) +sizeof(unsigned long) * sys_data->nb_args;

		entry = ktap_pre_trace(call, entry_size, &flags);
		entry->ent.type = call->event.type;
		entry->nr = syscall_nr;
		syscall_get_arguments(current, regs, 0, sys_data->nb_args,
				      (unsigned long *)&entry->args);

		ktap_do_trace(call, (void *)entry, entry_size, 0);

		ktap_post_trace(call, entry, &flags);
	} else {
		struct syscall_trace_exit *entry;

		call = sys_data->exit_event;

		entry = ktap_pre_trace(call, sizeof(*entry), &flags);
		entry->ent.type = call->event.type;
		entry->nr = syscall_nr;
		entry->ret = syscall_get_return_value(current, regs);

		ktap_do_trace(call, (void *)entry, sizeof(*entry), 0);

		ktap_post_trace(call, entry, &flags);
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
		struct syscall_metadata *meta;

		meta = syscall_nr_to_meta(i);
		if (!meta)
			continue;

		if (!strcmp(meta->name + 4, name))
			return i;
	}
	return -1;
}

/*
 * todo: see ARCH_HAS_SYSCALL_MATCH_SYM_NAME in trace_syscall.c
 * ppc64: not use sys_ prefix syscall, may have SyS
 */
static int get_syscall_info(struct ftrace_event_call *call, int *enter)
{
	char syscall_name[128] = {0};
	int syscall_nr;

	if (!strncmp(call->name, "sys_enter_", 10)) {
		*enter = 1;
		strncpy(syscall_name, call->name + 10, strlen(call->name) - 10);
	} else if (!strncmp(call->name, "sys_exit_", 9)) {
		*enter = 0;
		strncpy(syscall_name, call->name + 9, strlen(call->name) - 9);
	} else
		return -EINVAL;

	syscall_nr = find_syscall_nr(syscall_name);
	if (syscall_nr < 0)
		return -EINVAL;

	return syscall_nr;
}

int start_trace_syscalls(struct ftrace_event_call *call)
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

void stop_trace_syscalls(struct ftrace_event_call *call)
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
}

void ktap_init_syscalls(ktap_State *ks)
{
	Table *syscall_tbl;
	int i;

	/*
	 * init basic syscall ktap table
	 * syscall_talbe[syscall_nr]   = syscall_name
	 * syscall_talbe[syscall_name] = syscall_nr
	 */
	syscall_tbl = table_new(ks);
	table_resize(ks, syscall_tbl, NR_syscalls, NR_syscalls);

	for (i = 0; i < NR_syscalls; i++) {
		struct syscall_metadata *meta;
		Tvalue vn, vi;

		meta = syscall_nr_to_meta(i);
		if (!meta)
			continue;

		setsvalue(&vn, tstring_new(ks, meta->name));
        	table_setint(ks, syscall_tbl, i, &vn);

		setnvalue(&vi, i);
        	table_setvalue(ks, syscall_tbl, &vn, &vi);
	}
}

