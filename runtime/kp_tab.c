/*
 * kp_tab.c - Table handling.
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2014 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Adapted from luajit and lua interpreter.
 * Copyright (C) 2005-2014 Mike Pall.
 * Copyright (C) 1994-2008 Lua.org, PUC-Rio.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_vm.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_events.h"
#include "kp_tab.h"

#define tab_lock_init(t)						\
	do {								\
		(t)->lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;	\
	} while (0)
#define tab_lock(t)						\
	do {								\
		local_irq_save(flags);					\
		arch_spin_lock(&(t)->lock);				\
	} while (0)
#define tab_unlock(t)						\
	do {								\
		arch_spin_unlock(&(t)->lock);				\
		local_irq_restore(flags);				\
	} while (0)


const ktap_val_t kp_niltv = { {NULL}, {KTAP_TNIL} } ;
#define niltv  (&kp_niltv)

/* -- Object hashing ------------------------------------------------------ */

/* Hash values are masked with the table hash mask and used as an index. */
static __always_inline
ktap_node_t *hashmask(const ktap_tab_t *t, uint32_t hash)
{
	ktap_node_t *n = t->node;
	return &n[hash & t->hmask];
}

/* String hashes are precomputed when they are interned. */
#define hashstr(t, s)		hashmask(t, (s)->hash)

#define hashlohi(t, lo, hi)	hashmask((t), hashrot((lo), (hi)))
#define hashnum(t, o)		hashlohi((t), (o)->val.n & 0xffffffff, 0)
#define hashgcref(t, o)		hashlohi((t),	\
				((unsigned long)(o)->val.gc & 0xffffffff), \
				((unsigned long)(o)->val.gc & 0xffffffff) + HASH_BIAS)


/* Hash an arbitrary key and return its anchor position in the hash table. */
static ktap_node_t *hashkey(const ktap_tab_t *t, const ktap_val_t *key)
{
	kp_assert(!tvisint(key));
	if (is_string(key))
		return hashstr(t, rawtsvalue(key));
	else if (is_number(key))
		return hashnum(t, key);
	else if (is_bool(key))
		return hashmask(t, boolvalue(key));
	else
		return hashgcref(t, key);
}

/* -- Table creation and destruction -------------------------------------- */

/* Create new hash part for table. */
static __always_inline
int newhpart(ktap_state_t *ks, ktap_tab_t *t, uint32_t hbits)
{
	uint32_t hsize;
	ktap_node_t *node;
	kp_assert(hbits != 0);

	if (hbits > KP_MAX_HBITS) {
		kp_error(ks, "table overflow\n");
		return -1;
	}
	hsize = 1u << hbits;
	node = vmalloc(hsize * sizeof(ktap_node_t));
	if (!node)
		return -ENOMEM;
	t->freetop = &node[hsize];
	t->node = node;
	t->hmask = hsize-1;

	return 0;
}

/*
 * Q: Why all of these copies of t->hmask, t->node etc. to local variables?
 * A: Because alias analysis for C is _really_ tough.
 *    Even state-of-the-art C compilers won't produce good code without this.
 */

/* Clear hash part of table. */
static __always_inline void clearhpart(ktap_tab_t *t)
{
	uint32_t i, hmask = t->hmask;
	ktap_node_t *node = t->node;
	kp_assert(t->hmask != 0);

	for (i = 0; i <= hmask; i++) {
		ktap_node_t *n = &node[i];
		n->next = NULL;
		set_nil(&n->key);
		set_nil(&n->val);
	}

	t->hnum = 0;
}

/* Clear array part of table. */
static __always_inline void clearapart(ktap_tab_t *t)
{
	uint32_t i, asize = t->asize;
	ktap_val_t *array = t->array;
	for (i = 0; i < asize; i++)
		set_nil(&array[i]);
}

/* Create a new table. Note: the slots are not initialized (yet). */
static ktap_tab_t *newtab(ktap_state_t *ks, uint32_t asize, uint32_t hbits)
{
	ktap_tab_t *t;
 
	t = (ktap_tab_t *)kp_obj_new(ks, sizeof(ktap_tab_t));
	t->gct = ~KTAP_TTAB;
	t->array = NULL;
	t->asize = 0;  /* In case the array allocation fails. */
	t->hmask = 0;

	tab_lock_init(t);

	if (asize > 0) {
		if (asize > KP_MAX_ASIZE) {
			kp_error(ks, "table overflow\n");
			return NULL;
		}

		t->array = vmalloc(asize * sizeof(ktap_val_t));
		if (!t->array)
			return NULL;
		t->asize = asize;
	}
	if (hbits)
		if (newhpart(ks, t, hbits)) {
			vfree(t->array);
			return NULL;		
		}
	return t;
}

/* Create a new table.
 *
 * The array size is non-inclusive. E.g. asize=128 creates array slots
 * for 0..127, but not for 128. If you need slots 1..128, pass asize=129
 * (slot 0 is wasted in this case).
 *
 * The hash size is given in hash bits. hbits=0 means no hash part.
 * hbits=1 creates 2 hash slots, hbits=2 creates 4 hash slots and so on.
 */
ktap_tab_t *kp_tab_new(ktap_state_t *ks, uint32_t asize, uint32_t hbits)
{
	ktap_tab_t *t = newtab(ks, asize, hbits);
	if (!t)
		return NULL;

	clearapart(t);
	if (t->hmask > 0)
		clearhpart(t);
	return t;
}

#define TABLE_NARR_ENTRIES	255 /* PAGE_SIZE / sizeof(ktap_value) - 1 */
#define TABLE_NREC_ENTRIES	2048 /* (PAGE_SIZE * 20) / sizeof(ktap_tnode)*/

ktap_tab_t *kp_tab_new_ah(ktap_state_t *ks, int32_t a, int32_t h)
{
	if (a == 0 && h == 0) {
		a = TABLE_NARR_ENTRIES;
		h = TABLE_NREC_ENTRIES;
	}

	return kp_tab_new(ks, (uint32_t)(a > 0 ? a+1 : 0), hsize2hbits(h));
}

/* Duplicate a table. */
ktap_tab_t *kp_tab_dup(ktap_state_t *ks, const ktap_tab_t *kt)
{
	ktap_tab_t *t;
	uint32_t asize, hmask;
	int i;

	/* allocate default table size */
	t = kp_tab_new_ah(ks, 0, 0);
	if (!t)
		return NULL;

	asize = kt->asize;
	if (asize > 0) {
		ktap_val_t *array = t->array;
		ktap_val_t *karray = kt->array;
		if (asize < 64) {
			/* An inlined loop beats memcpy for < 512 bytes. */
			uint32_t i;
			for (i = 0; i < asize; i++)
				set_obj(&array[i], &karray[i]);
		} else {
			memcpy(array, karray, asize*sizeof(ktap_val_t));
		}
	}

	hmask = kt->hmask;
	for (i = 0; i <= hmask; i++) {
		ktap_node_t *knode = &kt->node[i];
		if (is_nil(&knode->key))
			continue;
		kp_tab_set(ks, t, &knode->key, &knode->val);
	}
	return t;
}

/* Clear a table. */
void kp_tab_clear(ktap_tab_t *t)
{
	clearapart(t);
	if (t->hmask > 0) {
		ktap_node_t *node = t->node;
		t->freetop = &node[t->hmask+1];
		clearhpart(t);
	}
}

/* Free a table. */
void kp_tab_free(ktap_state_t *ks, ktap_tab_t *t)
{
	if (t->hmask > 0)
		vfree(t->node);
	if (t->asize > 0)
		vfree(t->array);
	kp_free(ks, t);
}

/* -- Table getters ------------------------------------------------------- */

static const ktap_val_t *tab_getinth(ktap_tab_t *t, uint32_t key)
{
	ktap_val_t k;
	ktap_node_t *n;

	set_number(&k, (ktap_number)key);
	n = hashnum(t, &k);
	do {
		if (is_number(&n->key) && nvalue(&n->key) == key) {
			return &n->val;
		}
	} while ((n = n->next));
	return niltv;
}

static __always_inline
const ktap_val_t *tab_getint(ktap_tab_t *t, uint32_t key)
{
	return ((key < t->asize) ? arrayslot(t, key) :
				   tab_getinth(t, key));
}

void kp_tab_getint(ktap_tab_t *t, uint32_t key, ktap_val_t *val)
{
	unsigned long flags;

	tab_lock(t);
	set_obj(val, tab_getint(t, key));
	tab_unlock(t);
}

static const ktap_val_t *tab_getstr(ktap_tab_t *t, ktap_str_t *key)
{
	ktap_node_t *n = hashstr(t, key);
	do {
		if (is_string(&n->key) && rawtsvalue(&n->key) == key)
			return &n->val;
	} while ((n = n->next));
	return niltv;
}

void kp_tab_getstr(ktap_tab_t *t, ktap_str_t *key, ktap_val_t *val)
{
	unsigned long flags;

	tab_lock(t);
	set_obj(val,  tab_getstr(t, key));
	tab_unlock(t);
}

static const ktap_val_t *tab_get(ktap_state_t *ks, ktap_tab_t *t,
				 const ktap_val_t *key)
{
	if (is_string(key)) {
		return tab_getstr(t, rawtsvalue(key));
	} else if (is_number(key)) {
		ktap_number nk = nvalue(key);
		uint32_t k = (uint32_t)nk;
		if (nk == (ktap_number)k) {
			return tab_getint(t, k);
		} else {
			goto genlookup;	/* Else use the generic lookup. */
		}
	} else if (is_eventstr(key)) {
		const ktap_str_t *ts;

		if (!ks->current_event) {
			kp_error(ks,
			"cannot stringify event str in invalid context\n");
			return niltv;
		}

		ts = kp_event_stringify(ks);
		if (!ts)
			return niltv;

		return tab_getstr(t, rawtsvalue(key));
	} else if (!is_nil(key)) {
		ktap_node_t *n;
 genlookup:
		n = hashkey(t, key);
		do {
			if (kp_obj_equal(&n->key, key))
				return &n->val;
		} while ((n = n->next));
	}
	return niltv;
}

void kp_tab_get(ktap_state_t *ks, ktap_tab_t *t, const ktap_val_t *key,
		ktap_val_t *val)
{
	unsigned long flags;

	tab_lock(t);
	set_obj(val, tab_get(ks, t, key));
	tab_unlock(t);
}

/* -- Table setters ------------------------------------------------------- */

/* Insert new key. Use Brent's variation to optimize the chain length. */
static ktap_val_t *kp_tab_newkey(ktap_state_t *ks, ktap_tab_t *t,
				 const ktap_val_t *key)
{
	ktap_node_t *n = hashkey(t, key);

	if (!is_nil(&n->val) || t->hmask == 0) {
		ktap_node_t *nodebase = t->node;
		ktap_node_t *collide, *freenode = t->freetop;

		kp_assert(freenode >= nodebase &&
			  freenode <= nodebase+t->hmask+1);
		do {
			if (freenode == nodebase) {  /* No free node found? */
				kp_error(ks, "table overflow\n");
				return NULL;
			}
		} while (!is_nil(&(--freenode)->key));

		t->freetop = freenode;
		collide = hashkey(t, &n->key);
		if (collide != n) {  /* Colliding node not the main node? */
			while (collide->next != n)
				/* Find predecessor. */
				collide = collide->next;
			collide->next = freenode;  /* Relink chain. */
 			/* Copy colliding node into free node and
			 * free main node. */
			freenode->val = n->val;
			freenode->key = n->key;
			freenode->next = n->next;
			n->next = NULL;
			set_nil(&n->val);
			/* Rechain pseudo-resurrected string keys with
			 * colliding hashes. */
			while (freenode->next) {
				ktap_node_t *nn = freenode->next;
				if (is_string(&nn->key) && !is_nil(&nn->val) &&
					hashstr(t, rawtsvalue(&nn->key)) == n) {
					freenode->next = nn->next;
					nn->next = n->next;
					n->next = nn;
				} else {
					freenode = nn;
				}
			}
		} else {  /* Otherwise use free node. */
			freenode->next = n->next;  /* Insert into chain. */
			n->next = freenode;
			n = freenode;
		}
	}
	set_obj(&n->key, key);
	t->hnum++;
	return &n->val;
}

static ktap_val_t *tab_setinth(ktap_state_t *ks, ktap_tab_t *t, uint32_t key)
{
	ktap_val_t k;
	ktap_node_t *n;

	set_number(&k, (ktap_number)key);
	n = hashnum(t, &k);
	do {
		if (is_number(&n->key) && nvalue(&n->key) == key)
			return &n->val;
	} while ((n = n->next));
	return kp_tab_newkey(ks, t, &k);
}

static __always_inline
ktap_val_t *tab_setint(ktap_state_t *ks, ktap_tab_t *t, uint32_t key)
{
	return ((key < t->asize) ? arrayslot(t, key) :
				   tab_setinth(ks, t, key));
}

void kp_tab_setint(ktap_state_t *ks, ktap_tab_t *t,
		   uint32_t key, const ktap_val_t *val)
{
	ktap_val_t *v;
	unsigned long flags;

	tab_lock(t);
	v = tab_setint(ks, t, key);
	if (likely(v))
		set_obj(v, val);
	tab_unlock(t);
}

void kp_tab_incrint(ktap_state_t *ks, ktap_tab_t *t, uint32_t key,
		    ktap_number n)
{
	ktap_val_t *v;
	unsigned long flags;

	tab_lock(t);
	v = tab_setint(ks, t, key);
	if (unlikely(!v))
		goto out;

	if (likely(is_number(v)))
		set_number(v, nvalue(v) + n);
	else if (is_nil(v))
		set_number(v, n);
	else
		kp_error(ks, "use '+=' operator on non-number value\n");

 out:
	tab_unlock(t);
}

static ktap_val_t *tab_setstr(ktap_state_t *ks, ktap_tab_t *t,
			      const ktap_str_t *key)
{
	ktap_val_t k;
	ktap_node_t *n = hashstr(t, key);
	do {
		if (is_string(&n->key) && rawtsvalue(&n->key) == key)
			return &n->val;
	} while ((n = n->next));
	set_string(&k, key);
	return kp_tab_newkey(ks, t, &k);
}

void kp_tab_setstr(ktap_state_t *ks, ktap_tab_t *t, const ktap_str_t *key,
		   const ktap_val_t *val)
{
	ktap_val_t *v;
	unsigned long flags;

	tab_lock(t);
	v = tab_setstr(ks, t, key);
	if (likely(v))
		set_obj(v, val);
	tab_unlock(t);
}

void kp_tab_incrstr(ktap_state_t *ks, ktap_tab_t *t, const ktap_str_t *key,
		    ktap_number n)
{
	ktap_val_t *v;
	unsigned long flags;

	tab_lock(t);
	v = tab_setstr(ks, t, key);
	if (unlikely(!v))
		goto out;

	if (likely(is_number(v)))
		set_number(v, nvalue(v) + n);
	else if (is_nil(v))
		set_number(v, n);
	else
		kp_error(ks, "use '+=' operator on non-number value\n");
 out:
	tab_unlock(t);
}

static ktap_val_t *tab_set(ktap_state_t *ks, ktap_tab_t *t,
			   const ktap_val_t *key)
{
	ktap_node_t *n;

	if (is_string(key)) {
		return tab_setstr(ks, t, rawtsvalue(key));
	} else if (is_number(key)) {
		ktap_number nk = nvalue(key);
		uint32_t k = (ktap_number)nk;
		if (nk == (ktap_number)k)
			return tab_setint(ks, t, k);
	} else if (itype(key) == KTAP_TKSTACK) {
		/* change stack into string */
		ktap_str_t *bt = kp_obj_kstack2str(ks, key->val.stack.depth,
						       key->val.stack.skip);
		if (!bt)
			return NULL;
		return tab_setstr(ks, t, bt);
	} else if (is_eventstr(key)) {
		const ktap_str_t *ts;

		if (!ks->current_event) {
			kp_error(ks,
			"cannot stringify event str in invalid context\n");
			return NULL;
		}

		ts = kp_event_stringify(ks);
		if (!ts)
			return NULL;

		return tab_setstr(ks, t, ts);
		/* Else use the generic lookup. */
	} else if (is_nil(key)) {
		kp_error(ks, "table nil index\n");
		return NULL;
	}
	n = hashkey(t, key);
	do {
		if (kp_obj_equal(&n->key, key))
			return &n->val;
	} while ((n = n->next));
	return kp_tab_newkey(ks, t, key);
}

void kp_tab_set(ktap_state_t *ks, ktap_tab_t *t,
		const ktap_val_t *key, const ktap_val_t *val)
{
	ktap_val_t *v;
	unsigned long flags;

	tab_lock(t);
	v = tab_set(ks, t, key);
	if (likely(v))
		set_obj(v, val);
	tab_unlock(t);
}

void kp_tab_incr(ktap_state_t *ks, ktap_tab_t *t, ktap_val_t *key,
		 ktap_number n)
{
	ktap_val_t *v;
	unsigned long flags;

	tab_lock(t);
	v = tab_set(ks, t, key);
	if (unlikely(!v))
		goto out;

	if (likely(is_number(v)))
		set_number(v, nvalue(v) + n);
	else if (is_nil(v))
		set_number(v, n);
	else
		kp_error(ks, "use '+=' operator on non-number value\n");
 out:
	tab_unlock(t);
}


/* -- Table traversal ----------------------------------------------------- */

/* Get the traversal index of a key. */
static uint32_t keyindex(ktap_state_t *ks, ktap_tab_t *t,
			 const ktap_val_t *key)
{
	if (is_number(key)) {
		ktap_number nk = nvalue(key);
		uint32_t k = (uint32_t)nk;
		/* Array key indexes: [0..t->asize-1] */
		if ((uint32_t)k < t->asize && nk == (ktap_number)k)
			return (uint32_t)k;
	}

	if (!is_nil(key)) {
		ktap_node_t *n = hashkey(t, key);
		do {
			if (kp_obj_equal(&n->key, key))
				return t->asize + (uint32_t)(n - (t->node));
			/* Hash key indexes: [t->asize..t->asize+t->nmask] */
		} while ((n = n->next));
		kp_error(ks, "table next index\n");
		return 0;  /* unreachable */
	}
	return ~0u;  /* A nil key starts the traversal. */
}

/* Advance to the next step in a table traversal. */
int kp_tab_next(ktap_state_t *ks, ktap_tab_t *t, ktap_val_t *key)
{
	unsigned long flags;
	uint32_t i;

	tab_lock(t);
	i = keyindex(ks, t, key);  /* Find predecessor key index. */

	/* First traverse the array keys. */
	for (i++; i < t->asize; i++)
 		if (!is_nil(arrayslot(t, i))) {
			set_number(key, i);
			set_obj(key + 1, arrayslot(t, i));
			tab_unlock(t);
			return 1;
		}
	/* Then traverse the hash keys. */
	for (i -= t->asize; i <= t->hmask; i++) {
		ktap_node_t *n = &t->node[i];
		if (!is_nil(&n->val)) {
			set_obj(key, &n->key);
			set_obj(key + 1, &n->val);
			tab_unlock(t);
			return 1;
		}
	}
	tab_unlock(t);
	return 0;  /* End of traversal. */
}

/* -- Table length calculation -------------------------------------------- */

int kp_tab_len(ktap_state_t *ks, ktap_tab_t *t)
{
	unsigned long flags;
	int i, len = 0;

	tab_lock(t);
	for (i = 0; i < t->asize; i++) {
		ktap_val_t *v = &t->array[i];

		if (is_nil(v))
			continue;
		len++;
	}

	for (i = 0; i <= t->hmask; i++) {
		ktap_node_t *n = &t->node[i];

		if (is_nil(&n->key))
			continue;

		len++;
	}
	tab_unlock(t);
	return len;
}

static void string_convert(char *output, const char *input)
{
	if (strlen(input) > 32) {
		strncpy(output, input, 32-4);
		memset(output + 32-4, '.', 3);
	} else
		memcpy(output, input, strlen(input));
}

typedef struct ktap_node2 {
	ktap_val_t key;
	ktap_val_t val;
} ktap_node2_t;

static int hist_record_cmp(const void *i, const void *j)
{
	ktap_number n1 = nvalue(&((const ktap_node2_t *)i)->val);
	ktap_number n2 = nvalue(&((const ktap_node2_t *)j)->val);

	if (n1 == n2)
		return 0;
	else if (n1 < n2)
		return 1;
	else
		return -1;
}

/* todo: make histdump to be faster, just need to sort n entries, not all */

/* print_hist: key should be number/string/ip, value must be number */
static void tab_histdump(ktap_state_t *ks, ktap_tab_t *t, int shownums)
{
	long start_time, delta_time;
	uint32_t i, asize = t->asize;
	ktap_val_t *array = t->array;
	uint32_t hmask = t->hmask;
	ktap_node_t *node = t->node;
	ktap_node2_t *sort_mem;
	char dist_str[39];
	int total = 0, sum = 0;

	start_time = gettimeofday_ns();

	sort_mem = kmalloc((t->asize + t->hnum) * sizeof(ktap_node2_t),
				GFP_KERNEL);
	if (!sort_mem)
		return;

	/* copy all values in table into sort_mem. */
	for (i = 0; i < asize; i++) {
		ktap_val_t *val = &array[i];
		if (is_nil(val))
			continue;

		if (!is_number(val)) {
			kp_error(ks, "print_hist only can print number\n");
			goto out;
		}

		set_number(&sort_mem[total].key, i);
		set_obj(&sort_mem[total].val, val);
		sum += nvalue(val);
		total++;
	}

	for (i = 0; i <= hmask; i++) {
		ktap_node_t *n = &node[i];
		ktap_val_t *val = &n->val;

		if (is_nil(val))
			continue;

		if (!is_number(val)) {
			kp_error(ks, "print_hist only can print number\n");
			goto out;
		}

		set_obj(&sort_mem[total].key, &n->key);
		set_obj(&sort_mem[total].val, val);
		sum += nvalue(val);
		total++;
	}

	/* sort */
	sort(sort_mem, total, sizeof(ktap_node2_t), hist_record_cmp, NULL);

	dist_str[sizeof(dist_str) - 1] = '\0';

	for (i = 0; i < total; i++) {
		ktap_val_t *key = &sort_mem[i].key;
		ktap_number num = nvalue(&sort_mem[i].val);
		int ratio;

		if (!--shownums)
			break;

		memset(dist_str, ' ', sizeof(dist_str) - 1);
		ratio = (num * (sizeof(dist_str) - 1)) / sum;
		memset(dist_str, '@', ratio);

		if (is_string(key)) {
			//char buf[32] = {0};

			//string_convert(buf, svalue(key));
			if (rawtsvalue(key)->len > 32) {
				kp_puts(ks, svalue(key));
				kp_printf(ks, "%s\n%d\n\n", dist_str, num);
			} else {
				kp_printf(ks, "%31s |%s%-7d\n", svalue(key),
								dist_str, num);
			}
		} else if (is_number(key)) {
			kp_printf(ks, "%31d |%s%-7d\n", nvalue(key),
						dist_str, num);
		} else if (is_kip(key)) {
			char str[KSYM_SYMBOL_LEN];
			char buf[32] = {0};

			SPRINT_SYMBOL(str, nvalue(key));
			string_convert(buf, str);
			kp_printf(ks, "%31s |%s%-7d\n", buf, dist_str, num);
		}
	}

	if (!shownums && total)
		kp_printf(ks, "%31s |\n", "...");

 out:
	kfree(sort_mem);

	delta_time = (gettimeofday_ns() - start_time) / NSEC_PER_USEC;
	kp_verbose_printf(ks, "tab_histdump time: %d (us)\n", delta_time);
}

#define DISTRIBUTION_STR "------------- Distribution -------------"
void kp_tab_print_hist(ktap_state_t *ks, ktap_tab_t *t, int n)
{
	kp_printf(ks, "%31s%s%s\n", "value ", DISTRIBUTION_STR, " count");
	tab_histdump(ks, t, n);
}

