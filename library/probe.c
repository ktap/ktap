/*
 * probe.c - ktap probing core implementation
 *
 * Copyright (C) 2012 Jovi Zhang <bookjovi@gmail.com>
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

#include <linux/kprobes.h>
#include "../ktap.h"

struct ktap_kprobe {
	struct list_head list;
	struct kprobe p;
	ktap_State *ks;
	Closure *cl;
};

/* kprobe handler is called in interrupt disabled? */
static int __kprobes pre_handler_kprobe(struct kprobe *p, struct pt_regs *regs)
{
	struct ktap_kprobe *kp;
	ktap_State *ks;

	if (unlikely(__this_cpu_read(ktap_in_tracing)))
		return NULL;

	__this_cpu_write(ktap_in_tracing, true);

	kp = container_of(p, struct ktap_kprobe, p);

	if (same_thread_group(current, G(kp->ks)->task))
		goto out;

	ks = ktap_newthread(kp->ks);
	setcllvalue(ks->top, kp->cl);
	incr_top(ks);
	ktap_call(ks, ks->top - 1, 0);
	ktap_exitthread(ks);

 out:
	__this_cpu_write(ktap_in_tracing, false);
	return 0;
}

static int start_kprobe(ktap_State *ks, const char *event_name, Closure *cl)
{
	struct ktap_kprobe *kp;

	kp = ktap_zalloc(ks, sizeof(*kp));
	kp->ks = ks;
	kp->cl = cl;

	INIT_LIST_HEAD(&kp->list);
	list_add(&kp->list, &(G(ks)->kprobes));

	kp->p.symbol_name = event_name;
	kp->p.pre_handler = pre_handler_kprobe;
	kp->p.post_handler = NULL;
	kp->p.fault_handler = NULL;
	kp->p.break_handler = NULL;

	if (register_kprobe(&kp->p)) {
		ktap_printf(ks, "Cannot register probe: %s\n", event_name);
		list_del(&kp->list);
		return -1;
	}

	return 0;
}

int start_probe(ktap_State *ks, const char *event_name, Closure *cl)
{

	if (!strncmp(event_name, "kprobe:", 7))
		start_kprobe(ks, event_name + 7, cl);
	else {
		ktap_printf(ks, "unknown probe event name: %s\n", event_name);
		return -1;
	}

}
void end_probes(struct ktap_State *ks)
{
	struct list_head *kprobes_list = &(G(ks)->kprobes);
	struct ktap_kprobe *kp, *tmp;

	list_for_each_entry(kp, kprobes_list, list) {
		unregister_kprobe(&kp->p);
	}

	synchronize_sched();

	list_for_each_entry_safe(kp, tmp, kprobes_list, list) {
		list_del(&kp->list);
		ktap_free(ks, kp);
	}
}

int ktap_probe_init(ktap_State *ks)
{
	return 0;
}


