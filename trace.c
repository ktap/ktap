/*
 * trace.c - ktap tracing core implementation
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
#include <linux/ftrace_event.h>

#include "../trace.h"
#include "ktap.h"

struct ktap_trace_list {
	struct ftrace_event_call *call;
	ktap_Callback_data *cbdata;
	struct list_head list;
};

/* todo: put this list to global ktap State and percpu? */
static LIST_HEAD(trace_list);

static void call_user_closure(ktap_State *mainthread, Closure *cl,
			      void *entry, struct list_head *head)
{
	ktap_State *ks;
	Tvalue *func;
	struct ftrace_event_field *field;
	int numparames = cl->l.p->numparams;

	ks = ktap_newthread(mainthread);
	setcllvalue(ks->top, cl);
	func = ks->top;
	incr_top(ks);

	list_for_each_entry_reverse(field, head, link) {
		if (field->size == 4) {
			int v = *(int *)((unsigned char *)entry + field->offset);
			if (numparames > 0) {
				setnvalue(ks->top, v);
				incr_top(ks);
				numparames--;
			} else
				break;
		}
	}

	ktap_call(ks, func, 0);
	ktap_exitthread(ks);
}

/* core probe function called by tracepoint */
/*
 * todo: improve performance on ktap_do_trace
 */
static void ktap_do_trace(struct ftrace_event_call *call, void *entry,
			  const char *fmt, ...)
{
	ktap_Callback_data *cbdata;
	struct hlist_node *pos;
	char buff[512] = {0};
	int buff_inited = 0;

	hlist_for_each_entry_rcu(cbdata, pos,
				 &call->ktap_callback_list, node) {
		ktap_State *ks = cbdata->ks;
		Closure *cl = cbdata->cl;

		/* todo: trace_get_fields is not exported right now */
		if (cl) {
			call_user_closure(ks, cl, entry, trace_get_fields(call));
		} else {
			if (!buff_inited) {
				va_list args;

				va_start(args, fmt);
				vscnprintf(buff, 512, fmt, args);
				va_end(args);
				buff_inited = 1;
			}

			ktap_printf(ks, buff);
		}
	}
}

static void enable_event(struct ftrace_event_call *call, void *data)
{
	ktap_Callback_data *cbdata = data;
	
	if (!call->ktap_refcount) {
		struct ktap_trace_list *ktl;

		ktl = kmalloc(sizeof(struct ktap_trace_list), GFP_KERNEL);
		if (!ktl) {
			ktap_printf(cbdata->ks, "allocate ktap_trace_list failed\n");
			return;
		}
		ktl->call = call;
		ktl->cbdata = data;
		INIT_LIST_HEAD(&ktl->list);

		call->ktap_do_trace = ktap_do_trace;

		if (!call->class->ktap_probe) {
			/* syscall tracing */
			if (start_trace_syscalls(call, cbdata)) {
				kfree(ktl);
				return;
			}
		} else
			tracepoint_probe_register(call->name,
						  call->class->ktap_probe,
						  call);
		list_add(&ktl->list, &trace_list);
	}

	hlist_add_head_rcu(&cbdata->node, &call->ktap_callback_list);
	call->ktap_refcount++;
	cbdata->event_refcount++;
}

int start_trace(ktap_State *ks, char *event_name, Closure *cl)
{
	ktap_Callback_data *callback;

	callback = kzalloc(sizeof(ktap_Callback_data), GFP_KERNEL);
	callback->ks = ks;
	callback->cl = cl;

	ftrace_on_event_call(event_name, enable_event, (void *)callback);
	return 0;
}

/* cleanup all tracepoint owned by this ktap */
void end_all_trace(ktap_State *ks)
{
	struct ktap_trace_list *pos;

	if (list_empty(&trace_list))
		return;

	list_for_each_entry(pos, &trace_list, list) {
		struct ftrace_event_call *call = pos->call;
		ktap_Callback_data *cbdata = pos->cbdata;

		hlist_del_rcu(&cbdata->node);

		if (!--call->ktap_refcount) {
			if (!call->class->ktap_probe)
				stop_trace_syscalls(call, cbdata);
			else
				tracepoint_probe_unregister(call->name,
							    call->class->ktap_probe,
							    call);

			call->ktap_do_trace = NULL;
			hlist_empty(&call->ktap_callback_list);
		}

		if (!--cbdata->event_refcount)
			kfree(cbdata);
		kfree(pos);
	}

	tracepoint_synchronize_unregister();

	/* empty trace_list */	
	INIT_LIST_HEAD(&trace_list);
}

