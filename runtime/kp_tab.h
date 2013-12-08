#ifndef __KTAP_TAB_H__
#define __KTAP_TAB_H__

ktap_value *kp_tab_set(ktap_state *ks, ktap_tab *t, const ktap_value *key);
ktap_tab *kp_tab_new(ktap_state *ks);
const ktap_value *kp_tab_getint(ktap_tab *t, int key);
void kp_tab_setint(ktap_state *ks, ktap_tab *t, int key, ktap_value *v);
const ktap_value *kp_tab_get(ktap_tab *t, const ktap_value *key);
void kp_tab_setvalue(ktap_state *ks, ktap_tab *t, const ktap_value *key, ktap_value *val);
void kp_tab_resize(ktap_state *ks, ktap_tab *t, int nasize, int nhsize);
void kp_tab_resizearray(ktap_state *ks, ktap_tab *t, int nasize);
void kp_tab_free(ktap_state *ks, ktap_tab *t);
int kp_tab_length(ktap_state *ks, ktap_tab *t);
void kp_tab_dump(ktap_state *ks, ktap_tab *t);
void kp_tab_clear(ktap_state *ks, ktap_tab *t);
void kp_tab_histogram(ktap_state *ks, ktap_tab *t);
int kp_tab_next(ktap_state *ks, ktap_tab *t, StkId key);
int kp_tab_sort_next(ktap_state *ks, ktap_tab *t, StkId key);
void kp_tab_sort(ktap_state *ks, ktap_tab *t, ktap_closure *cmp_func);
void kp_tab_atomic_inc(ktap_state *ks, ktap_tab *t, ktap_value *key, int n);
void kp_statdata_dump(ktap_state *ks, ktap_stat_data *sd);
ktap_ptab *kp_ptab_new(ktap_state *ks);
ktap_tab *kp_ptab_synthesis(ktap_state *ks, ktap_ptab *ph);
void kp_ptab_dump(ktap_state *ks, ktap_ptab *ph);
void kp_ptab_free(ktap_state *ks, ktap_ptab *ph);
void kp_ptab_set(ktap_state *ks, ktap_ptab *ph,
			ktap_value *key, ktap_value *val);
void kp_ptab_get(ktap_state *ks, ktap_ptab *ph,
			ktap_value *key, ktap_value *val);
void kp_ptab_histogram(ktap_state *ks, ktap_ptab *ph);

#endif /* __KTAP_TAB_H__ */
