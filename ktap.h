#ifndef __KTAP_H__
#define __KTAP_H__

#include "ktap_types.h"
#include "ktap_opcodes.h"

#include <linux/trace_seq.h>

typedef struct ktap_Reg {
        const char *name;
        ktap_cfunction func;
} ktap_Reg;

ktap_State *kp_newstate(ktap_State **private_data, int argc, char **argv);
void kp_exit(ktap_State *ks);
ktap_State *kp_newthread(ktap_State *mainthread);
void kp_exitthread(ktap_State *ks);
Closure *kp_load(ktap_State *ks, unsigned char *buff);
void kp_call(ktap_State *ks, StkId func, int nresults);
void kp_optimize_code(ktap_State *ks, int level, Proto *f);
void kp_register_lib(ktap_State *ks, const char *libname, const ktap_Reg *funcs);

void kp_init_baselib(ktap_State *ks);
void kp_init_oslib(ktap_State *ks);
void kp_init_kdebuglib(ktap_State *ks);
void kp_init_timerlib(ktap_State *ks);

void kp_event_handle(ktap_State *ks, void *e, int field, StkId ra);
int kp_probe_init(ktap_State *ks);
void kp_probe_exit(ktap_State *ks);

int kp_event_get_index(const char *field);
Tstring *kp_event_get_ts(ktap_State *ks, int index);

int kp_strfmt(ktap_State *ks, struct trace_seq *seq);

void kp_transport_write(ktap_State *ks, const void *data, size_t length);
void *kp_transport_reserve(ktap_State *ks, size_t length);
void kp_transport_exit(ktap_State *ks);
int kp_transport_init(ktap_State *ks);

void kp_user_complete(ktap_State *ks);

void kp_exit_timers(ktap_State *ks);

DECLARE_PER_CPU(bool, ktap_in_tracing);

void kp_show_event(ktap_State *ks);

#endif /* __KTAP_H__ */
