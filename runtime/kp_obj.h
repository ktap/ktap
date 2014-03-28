#ifndef __KTAP_OBJ_H__
#define __KTAP_OBJ_H__

void *kp_malloc(ktap_state_t *ks, int size);
void *kp_zalloc(ktap_state_t *ks, int size);
void kp_free(ktap_state_t *ks, void *addr);

void kp_obj_dump(ktap_state_t *ks, const ktap_val_t *v);
void kp_obj_show(ktap_state_t *ks, const ktap_val_t *v);
int kp_obj_len(ktap_state_t *ks, const ktap_val_t *rb);
ktap_obj_t *kp_obj_new(ktap_state_t *ks, size_t size);
int kp_obj_rawequal(const ktap_val_t *t1, const ktap_val_t *t2);
ktap_str_t *kp_obj_kstack2str(ktap_state_t *ks, uint16_t depth, uint16_t skip);
void kp_obj_freeall(ktap_state_t *ks);

#define kp_obj_equal(o1, o2) \
	(((o1)->type == (o2)->type) && kp_obj_rawequal(o1, o2))

#endif /* __KTAP_OBJ_H__ */
