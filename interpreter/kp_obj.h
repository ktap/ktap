#ifndef __KTAP_OBJ_H__
#define __KTAP_OBJ_H__

#ifdef __KERNEL__
void *kp_malloc(ktap_state *ks, int size);
void kp_free(ktap_state *ks, void *addr);
void *kp_reallocv(ktap_state *ks, void *addr, int oldsize, int newsize);
void *kp_zalloc(ktap_state *ks, int size);
#else
#define kp_malloc(ks, size)			malloc(size)
#define kp_free(ks, block)			free(block)
#define kp_reallocv(ks, block, osize, nsize)	realloc(block, nsize)
#endif

void kp_obj_dump(ktap_state *ks, const ktap_value *v);
void kp_showobj(ktap_state *ks, const ktap_value *v);
int kp_objlen(ktap_state *ks, const ktap_value *rb);
void kp_objclone(ktap_state *ks, const ktap_value *o, ktap_value *newo,
		 ktap_gcobject **list);
ktap_gcobject *kp_newobject(ktap_state *ks, int type, size_t size, ktap_gcobject **list);
int kp_equalobjv(ktap_state *ks, const ktap_value *t1, const ktap_value *t2);
ktap_closure *kp_newclosure(ktap_state *ks, int n);
ktap_proto *kp_newproto(ktap_state *ks);
ktap_upval *kp_newupval(ktap_state *ks);
void kp_free_gclist(ktap_state *ks, ktap_gcobject *o);
void kp_free_all_gcobject(ktap_state *ks);
void kp_header(u8 *h);

#endif /* __KTAP_OBJ_H__ */
