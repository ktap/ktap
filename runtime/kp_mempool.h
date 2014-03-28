#ifndef __KTAP_MEMPOOL_H__
#define __KTAP_MEMPOOL_H__

void *kp_mempool_alloc(ktap_state_t *ks, int size);
void kp_mempool_destroy(ktap_state_t *ks);
int kp_mempool_init(ktap_state_t *ks, int size);

#endif /* __KTAP_MEMPOOL_H__ */
