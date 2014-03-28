#ifndef __KTAP_VM_H__
#define __KTAP_VM_H__

#include "kp_obj.h"

void kp_vm_call_proto(ktap_state_t *ks, ktap_proto_t *pt);
void kp_vm_call(ktap_state_t *ks, StkId func, int nresults);
int kp_vm_validate_code(ktap_state_t *ks, ktap_proto_t *pt, ktap_val_t *base);
void kp_vm_exit(ktap_state_t *ks);
ktap_state_t *kp_vm_new_state(ktap_option_t *parm, struct dentry *dir);
void kp_optimize_code(ktap_state_t *ks, int level, ktap_proto_t *f);
int kp_vm_register_lib(ktap_state_t *ks, const char *libname,
		       const ktap_libfunc_t *funcs);


static __always_inline
ktap_state_t *kp_vm_new_thread(ktap_state_t *mainthread, int rctx)
{
	ktap_state_t *ks;

	ks = kp_this_cpu_state(mainthread, rctx);
	ks->top = ks->stack;
	return ks;
}

static __always_inline
void kp_vm_exit_thread(ktap_state_t *ks)
{
}

/*
 * This function only tell ktapvm this thread want to exit,
 * let mainthread handle real exit work later.
 */
static __always_inline
void kp_vm_try_to_exit(ktap_state_t *ks)
{
	G(ks)->mainthread->stop = 1;
	G(ks)->state = KTAP_EXIT;
}


#endif /* __KTAP_VM_H__ */
