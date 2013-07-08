#ifndef __KTAP_H__
#define __KTAP_H__

#include "ktap_types.h"
#include "ktap_opcodes.h"

#include <linux/trace_seq.h>

typedef struct ktap_Reg {
        const char *name;
        ktap_cfunction func;
} ktap_Reg;

struct ktap_probe_event {
	struct list_head list;
	int type;
	const char *name;
	struct perf_event *perf;
	ktap_state *ks;
	ktap_closure *cl;
};

/* this structure allocate on stack */
struct ktap_event {
	struct ktap_probe_event *pevent;
	struct ftrace_event_call *call;
	void *entry;
	int entry_size;
	struct pt_regs *regs;
};

enum {
	KTAP_PERCPU_DATA_STATE,
	KTAP_PERCPU_DATA_STACK,
	KTAP_PERCPU_DATA_BUFFER,

	KTAP_PERCPU_DATA_MAX
};

ktap_state *kp_newstate(ktap_state **private_data, struct ktap_parm *parm, char **argv);
void kp_exit(ktap_state *ks);
ktap_state *kp_newthread(ktap_state *mainthread);
void kp_exitthread(ktap_state *ks);
ktap_closure *kp_load(ktap_state *ks, unsigned char *buff);
void kp_call(ktap_state *ks, StkId func, int nresults);
void kp_optimize_code(ktap_state *ks, int level, ktap_proto *f);
void kp_register_lib(ktap_state *ks, const char *libname, const ktap_Reg *funcs);
void *kp_percpu_data(int type);

void kp_init_baselib(ktap_state *ks);
void kp_init_oslib(ktap_state *ks);
void kp_init_kdebuglib(ktap_state *ks);
void kp_init_timerlib(ktap_state *ks);

void kp_event_handle(ktap_state *ks, void *e, int field, StkId ra);
int kp_probe_init(ktap_state *ks);
void kp_probe_exit(ktap_state *ks);

int kp_event_get_index(const char *field);
ktap_string *kp_event_get_ts(ktap_state *ks, int index);

int kp_strfmt(ktap_state *ks, struct trace_seq *seq);

void kp_transport_write(ktap_state *ks, const void *data, size_t length);
void kp_transport_event_write(ktap_state *ks, struct ktap_event *e);
void *kp_transport_reserve(ktap_state *ks, size_t length);
void kp_transport_exit(ktap_state *ks);
int kp_transport_init(ktap_state *ks);

void kp_user_complete(ktap_state *ks);

void kp_exit_timers(ktap_state *ks);

DECLARE_PER_CPU(bool, ktap_in_tracing);

#define kp_verbose_printf(ks, ...) \
	if (G(ks)->verbose)	\
		kp_printf(ks, __VA_ARGS__);

/* get argument operation macro */
#define kp_arg(ks, n)	((ks)->ci->func + (n))
#define kp_arg_nr(ks)	((int)(ks->top - (ks->ci->func + 1)))

#endif /* __KTAP_H__ */
