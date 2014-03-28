#ifndef __KTAP_STR_H__
#define __KTAP_STR_H__

int kp_str_resize(ktap_state_t *ks, int newmask);
void kp_str_freeall(ktap_state_t *ks);
ktap_str_t * kp_str_new(ktap_state_t *ks, const char *str, size_t len);

#define kp_str_newz(ks, s)	(kp_str_new(ks, s, strlen(s)))

#include <linux/trace_seq.h>
int kp_str_fmt(ktap_state_t *ks, struct trace_seq *seq);

#endif /* __KTAP_STR_H__ */
