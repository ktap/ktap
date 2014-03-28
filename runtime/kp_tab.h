#ifndef __KTAP_TAB_H__
#define __KTAP_TAB_H__

/* Hash constants. Tuned using a brute force search. */
#define HASH_BIAS       (-0x04c11db7)
#define HASH_ROT1       14
#define HASH_ROT2       5
#define HASH_ROT3       13

/* Every half-decent C compiler transforms this into a rotate instruction. */
#define kp_rol(x, n)    (((x)<<(n)) | ((x)>>(-(int)(n)&(8*sizeof(x)-1))))
#define kp_ror(x, n)    (((x)<<(-(int)(n)&(8*sizeof(x)-1))) | ((x)>>(n)))

/* Scramble the bits of numbers and pointers. */
static __always_inline uint32_t hashrot(uint32_t lo, uint32_t hi)
{
	/* Prefer variant that compiles well for a 2-operand CPU. */
	lo ^= hi; hi = kp_rol(hi, HASH_ROT1);
	lo -= hi; hi = kp_rol(hi, HASH_ROT2);
	hi ^= lo; hi -= kp_rol(lo, HASH_ROT3);
	return hi;
}


#define FLS(x)       ((uint32_t)(__builtin_clz(x)^31))
#define hsize2hbits(s)  ((s) ? ((s)==1 ? 1 : 1+FLS((uint32_t)((s)-1))) : 0)

#define arrayslot(t, i)         (&(t)->array[(i)])

void kp_tab_set(ktap_state_t *ks, ktap_tab_t *t, const ktap_val_t *key,
		const ktap_val_t *val);
void kp_tab_setstr(ktap_state_t *ks, ktap_tab_t *t,
		   const ktap_str_t *key, const ktap_val_t *val);
void kp_tab_incrstr(ktap_state_t *ks, ktap_tab_t *t, const ktap_str_t *key,
		    ktap_number n);
void kp_tab_get(ktap_state_t *ks, ktap_tab_t *t, const ktap_val_t *key,
		ktap_val_t *val);
void kp_tab_getstr(ktap_tab_t *t, ktap_str_t *key, ktap_val_t *val);

void kp_tab_getint(ktap_tab_t *t, uint32_t key, ktap_val_t *val);
void kp_tab_setint(ktap_state_t *ks, ktap_tab_t *t,
		   uint32_t key, const ktap_val_t *val);
void kp_tab_incrint(ktap_state_t *ks, ktap_tab_t *t, uint32_t key,
		    ktap_number n);
ktap_tab_t *kp_tab_new(ktap_state_t *ks, uint32_t asize, uint32_t hbits);
ktap_tab_t *kp_tab_new_ah(ktap_state_t *ks, int32_t a, int32_t h);
ktap_tab_t *kp_tab_dup(ktap_state_t *ks, const ktap_tab_t *kt);

void kp_tab_free(ktap_state_t *ks, ktap_tab_t *t);
int kp_tab_len(ktap_state_t *ks, ktap_tab_t *t);
void kp_tab_dump(ktap_state_t *ks, ktap_tab_t *t);
void kp_tab_clear(ktap_tab_t *t);
void kp_tab_print_hist(ktap_state_t *ks, ktap_tab_t *t, int n);
int kp_tab_next(ktap_state_t *ks, ktap_tab_t *t, StkId key);
int kp_tab_sort_next(ktap_state_t *ks, ktap_tab_t *t, StkId key);
void kp_tab_sort(ktap_state_t *ks, ktap_tab_t *t, ktap_func_t *cmp_func);
void kp_tab_incr(ktap_state_t *ks, ktap_tab_t *t, ktap_val_t *key,
		ktap_number n);
#endif /* __KTAP_TAB_H__ */
