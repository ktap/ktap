#ifndef __KTAP_EVENTS_H__
#define __KTAP_EVENTS_H__

#include "trace_events.h"
#include <trace/syscall.h>
#include <trace/events/syscalls.h>
#include <linux/syscalls.h>
#include <linux/kprobes.h>

enum KTAP_EVENT_FIELD_TYPE {
	KTAP_EVENT_FIELD_TYPE_INVALID = 0, /* arg type not support yet */

	KTAP_EVENT_FIELD_TYPE_INT,
	KTAP_EVENT_FIELD_TYPE_LONG,
	KTAP_EVENT_FIELD_TYPE_STRING,

	KTAP_EVENT_FIELD_TYPE_REGESTER,
	KTAP_EVENT_FIELD_TYPE_CONST,
	KTAP_EVENT_FIELD_TYPE_NIL /* arg not exist */
};

struct ktap_event_field {
	enum KTAP_EVENT_FIELD_TYPE type;
	int offset;
};

enum KTAP_EVENT_TYPE {
	KTAP_EVENT_TYPE_PERF,
	KTAP_EVENT_TYPE_TRACEPOINT,
	KTAP_EVENT_TYPE_SYSCALL_ENTER,
	KTAP_EVENT_TYPE_SYSCALL_EXIT,
	KTAP_EVENT_TYPE_KPROBE,
};

struct ktap_event {
	struct list_head list;
	int type;
	ktap_state_t *ks;
	ktap_func_t *fn;
	struct perf_event *perf;
	int syscall_nr; /* for syscall event */
	struct ktap_event_field fields[9]; /* arg1..arg9 */
	ktap_str_t *name; /* intern probename string */

	struct kprobe kp; /* kprobe event */
};

/* this structure allocate on stack */
struct ktap_event_data {
	struct ktap_event *event;
	struct perf_sample_data *data;
	struct pt_regs *regs;
	ktap_str_t *argstr; /* for cache argstr intern string */
};

int kp_events_init(ktap_state_t *ks);
void kp_events_exit(ktap_state_t *ks);

int kp_event_create(ktap_state_t *ks, struct perf_event_attr *attr,
		    struct task_struct *task, const char *filter,
		    ktap_func_t *fn);
int kp_event_create_tracepoint(ktap_state_t *ks, const char *event_name,
			       ktap_func_t *fn);

int kp_event_create_kprobe(ktap_state_t *ks, const char *event_name,
			   ktap_func_t *fn);
void kp_event_getarg(ktap_state_t *ks, ktap_val_t *ra, int idx);
const char *kp_event_tostr(ktap_state_t *ks);
const ktap_str_t *kp_event_stringify(ktap_state_t *ks);

#endif /* __KTAP_EVENTS_H__ */
