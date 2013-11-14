#ifndef __KTAP_TRANSPORT_H__
#define __KTAP_TRANSPORT_H__

void kp_transport_write(ktap_state *ks, const void *data, size_t length);
void kp_transport_event_write(ktap_state *ks, struct ktap_event *e);
void kp_transport_print_backtrace(ktap_state *ks, int skip, int max_entries);
void *kp_transport_reserve(ktap_state *ks, size_t length);
void kp_transport_exit(ktap_state *ks);
int kp_transport_init(ktap_state *ks, struct dentry *dir);

int _trace_seq_puts(struct trace_seq *s, const char *str);

#endif /* __KTAP_TRANSPORT_H__ */
