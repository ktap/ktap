#ifndef __KTAP_VM_H__
#define __KTAP_VM_H__

long gettimeofday_ns(void); /* common helper function */
ktap_state *kp_newstate(struct ktap_parm *parm, struct dentry *dir);
void kp_prepare_to_exit(ktap_state *ks);
void kp_init_exit_instruction(void);
void kp_final_exit(ktap_state *ks);
ktap_state *kp_thread_new(ktap_state *mainthread);
void kp_thread_exit(ktap_state *ks);
void kp_call(ktap_state *ks, StkId func, int nresults);
void kp_optimize_code(ktap_state *ks, int level, ktap_proto *f);
int kp_register_lib(ktap_state *ks, const char *libname, const ktap_Reg *funcs);

#endif /* __KTAP_VM_H__ */
