#ifndef __KTAP_VM_H__
#define __KTAP_VM_H__

int gettimeofday_us(void); /* common helper function */
ktap_state *kp_newstate(struct ktap_parm *parm, struct dentry *dir);
void kp_exit(ktap_state *ks);
void kp_init_exit_instruction(void);
void kp_final_exit(ktap_state *ks);
ktap_state *kp_newthread(ktap_state *mainthread);
void kp_exitthread(ktap_state *ks);
void kp_call(ktap_state *ks, StkId func, int nresults);
void kp_optimize_code(ktap_state *ks, int level, ktap_proto *f);
void kp_register_lib(ktap_state *ks, const char *libname,
			const ktap_Reg *funcs);

#endif /* __KTAP_VM_H__ */
