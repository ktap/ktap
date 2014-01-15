#ifndef __KTAP_OBJ_H__
#define __KTAP_OBJ_H__

void *kp_malloc(ktap_state *ks, int size);
void kp_free(ktap_state *ks, void *addr);
void *kp_reallocv(ktap_state *ks, void *addr, int oldsize, int newsize);
void *kp_zalloc(ktap_state *ks, int size);

void *kp_rawobj_alloc(ktap_state *ks, int size);

void kp_obj_dump(ktap_state *ks, const ktap_value *v);
void kp_obj_show(ktap_state *ks, const ktap_value *v);
int kp_obj_len(ktap_state *ks, const ktap_value *rb);
void kp_obj_clone(ktap_state *ks, const ktap_value *o, ktap_value *newo,
		 ktap_gcobject **list);
ktap_gcobject *kp_obj_newobject(ktap_state *ks, int type, size_t size, ktap_gcobject **list);
int kp_obj_equal(ktap_state *ks, const ktap_value *t1, const ktap_value *t2);
ktap_closure *kp_obj_newclosure(ktap_state *ks, int n);
ktap_proto *kp_obj_newproto(ktap_state *ks);
ktap_upval *kp_obj_newupval(ktap_state *ks);
void kp_obj_free_gclist(ktap_state *ks, ktap_gcobject *o);
void kp_obj_freeall(ktap_state *ks);

#endif /* __KTAP_OBJ_H__ */
