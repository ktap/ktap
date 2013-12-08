#ifndef __KTAP_STR_H__
#define __KTAP_STR_H__

ktap_string *kp_tstring_newlstr(ktap_state *ks, const char *str, size_t l);
ktap_string *kp_tstring_newlstr_local(ktap_state *ks, const char *str, size_t l);
ktap_string *kp_tstring_new(ktap_state *ks, const char *str);
ktap_string *kp_tstring_new_local(ktap_state *ks, const char *str);
int kp_tstring_eqstr(ktap_string *a, ktap_string *b);
unsigned int kp_string_hash(const char *str, size_t l, unsigned int seed);
int kp_tstring_eqlngstr(ktap_string *a, ktap_string *b);
int kp_tstring_cmp(const ktap_string *ls, const ktap_string *rs);
void kp_tstring_resize(ktap_state *ks, int newsize);
void kp_tstring_freeall(ktap_state *ks);

#ifdef __KERNEL__
#include <linux/trace_seq.h>
int kp_str_fmt(ktap_state *ks, struct trace_seq *seq);
#endif

#endif /* __KTAP_STR_H__ */
