#ifndef __KTAP_H__
#define __KTAP_H__

#include "ktap_types.h"
#include "ktap_opcodes.h"


typedef struct ktap_Callback_data {
	ktap_State *ks;
	Closure *cl;
	int event_refcount;
	struct hlist_node node;
} ktap_Callback_data;


typedef struct ktap_Reg {
        const char *name;
        ktap_cfunction func;
} ktap_Reg;



ktap_State *ktap_newstate(void);
void ktap_exit(ktap_State *ks);
ktap_State *ktap_newthread(ktap_State *mainthread);
void ktap_exitthread(ktap_State *ks);
Closure *ktap_load(ktap_State *ks, unsigned char *buff);
void ktap_call(ktap_State *ks, StkId func, int nresults);
void ktap_register_lib(ktap_State *ks, const char *libname, const ktap_Reg *funcs);

void ktap_init_syscalls(ktap_State *ks);
void ktap_init_baselib(ktap_State *ks);
void ktap_init_oslib(ktap_State *ks);

int start_trace_syscalls(struct ftrace_event_call *call,
			  ktap_Callback_data *callback);
void stop_trace_syscalls(struct ftrace_event_call *call,
			 ktap_Callback_data *callback);

int start_trace(ktap_State *ks, char *event_name, Closure *cl);
void end_all_trace(ktap_State *ks);

Tstring *ktap_strfmt(ktap_State *ks);

#endif /* __KTAP_H__ */
