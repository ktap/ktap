#ifndef __KTAP_TAB_H__
#define __KTAP_TAB_H__

ktap_value *kp_table_set(ktap_state *ks, ktap_table *t, const ktap_value *key);
ktap_table *kp_table_new(ktap_state *ks);
const ktap_value *kp_table_getint(ktap_table *t, int key);
void kp_table_setint(ktap_state *ks, ktap_table *t, int key, ktap_value *v);
const ktap_value *kp_table_get(ktap_table *t, const ktap_value *key);
void kp_table_setvalue(ktap_state *ks, ktap_table *t, const ktap_value *key, ktap_value *val);
void kp_table_resize(ktap_state *ks, ktap_table *t, int nasize, int nhsize);
void kp_table_resizearray(ktap_state *ks, ktap_table *t, int nasize);
void kp_table_free(ktap_state *ks, ktap_table *t);
int kp_table_length(ktap_state *ks, ktap_table *t);
void kp_table_dump(ktap_state *ks, ktap_table *t);
void kp_table_clear(ktap_state *ks, ktap_table *t);
void kp_table_histogram(ktap_state *ks, ktap_table *t);
int kp_table_next(ktap_state *ks, ktap_table *t, StkId key);
int kp_table_sort_next(ktap_state *ks, ktap_table *t, StkId key);
void kp_table_sort(ktap_state *ks, ktap_table *t, ktap_closure *cmp_func);
void kp_table_atomic_inc(ktap_state *ks, ktap_table *t, ktap_value *key, int n);
void kp_statdata_dump(ktap_state *ks, ktap_stat_data *sd);
ktap_ptable *kp_ptable_new(ktap_state *ks);
ktap_table *kp_ptable_synthesis(ktap_state *ks, ktap_ptable *ph);
void kp_ptable_dump(ktap_state *ks, ktap_ptable *ph);
void kp_ptable_free(ktap_state *ks, ktap_ptable *ph);
void kp_ptable_set(ktap_state *ks, ktap_ptable *ph,
			ktap_value *key, ktap_value *val);
void kp_ptable_get(ktap_state *ks, ktap_ptable *ph,
			ktap_value *key, ktap_value *val);
void kp_ptable_histogram(ktap_state *ks, ktap_ptable *ph);

#endif /* __KTAP_TAB_H__ */
