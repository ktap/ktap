#ifndef __KTAP_H__
#define __KTAP_H__

#include "ktap_types.h"
#include "ktap_opcodes.h"

typedef struct ktap_Reg {
        const char *name;
        ktap_cfunction func;
} ktap_Reg;

ktap_State *ktap_newstate(ktap_State **ks);
void ktap_exit(ktap_State *ks);
ktap_State *ktap_newthread(ktap_State *mainthread);
void ktap_exitthread(ktap_State *ks);
Closure *ktap_load(ktap_State *ks, unsigned char *buff);
void ktap_call(ktap_State *ks, StkId func, int nresults);
void ktap_optimize_code(ktap_State *ks, int level, Proto *f);
void ktap_register_lib(ktap_State *ks, const char *libname, const ktap_Reg *funcs);

void ktap_init_syscalls(ktap_State *ks);
void ktap_init_baselib(ktap_State *ks);
void ktap_init_oslib(ktap_State *ks);

void ktap_event_handle(ktap_State *ks, void *e, int field, StkId ra);
void ktap_do_trace(struct ftrace_event_call *call, void *entry,
			  int entry_size, int data_size);
int start_trace_syscalls(struct ftrace_event_call *call);
void stop_trace_syscalls(struct ftrace_event_call *call);
int ktap_trace_init(void);

int start_trace(ktap_State *ks, char *event_name, Closure *cl);
void end_all_trace(ktap_State *ks);
int ktap_event_get_index(const char *field);
Tstring *ktap_event_get_ts(ktap_State *ks, int index);

Tstring *ktap_strfmt(ktap_State *ks);

void ktap_transport_write(ktap_State *ks, const void *data, size_t length);
void *ktap_transport_reserve(ktap_State *ks, size_t length);
void ktap_transport_exit(ktap_State *ks);
int ktap_transport_init(ktap_State *ks);

void *ktap_pre_trace(struct ftrace_event_call *call, int size, unsigned long *flags);
void ktap_post_trace(struct ftrace_event_call *call, void *entry, unsigned long *flags);

void ktap_user_complete(ktap_State *ks);
#endif /* __KTAP_H__ */
