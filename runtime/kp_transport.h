#ifndef __KTAP_TRANSPORT_H__
#define __KTAP_TRANSPORT_H__

void kp_transport_write(ktap_state_t *ks, const void *data, size_t length);
void kp_transport_event_write(ktap_state_t *ks, struct ktap_event_data *e);
void kp_transport_print_kstack(ktap_state_t *ks, uint16_t depth, uint16_t skip);
void *kp_transport_reserve(ktap_state_t *ks, size_t length);
void kp_transport_exit(ktap_state_t *ks);
int kp_transport_init(ktap_state_t *ks, struct dentry *dir);

int _trace_seq_puts(struct trace_seq *s, const char *str);

#endif /* __KTAP_TRANSPORT_H__ */
