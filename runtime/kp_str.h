#ifndef __KTAP_STR_H__
#define __KTAP_STR_H__

ktap_string *kp_str_newlstr(ktap_state *ks, const char *str, size_t l);
ktap_string *kp_str_newlstr_local(ktap_state *ks, const char *str, size_t l);
ktap_string *kp_str_new(ktap_state *ks, const char *str);
ktap_string *kp_str_new_local(ktap_state *ks, const char *str);
int kp_str_eqstr(ktap_string *a, ktap_string *b);
unsigned int kp_str_hash(const char *str, size_t l, unsigned int seed);
int kp_str_eqlng(ktap_string *a, ktap_string *b);
int kp_str_cmp(const ktap_string *ls, const ktap_string *rs);
void kp_strtab_resize(ktap_state *ks, int newsize);
void kp_str_freeall(ktap_state *ks);

#include <linux/trace_seq.h>
int kp_str_fmt(ktap_state *ks, struct trace_seq *seq);

#endif /* __KTAP_STR_H__ */
