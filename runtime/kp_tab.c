/*
 * kp_tab.c - ktap table data structure manipulation
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Copyright (C) 1994-2013 Lua.org, PUC-Rio.
 *  - The part of code in this file is copied from lua initially.
 *  - lua's MIT license is compatible with GPL.
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
#include <linux/sort.h>
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_vm.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_tab.h"

#define kp_tab_lock_init(t)						\
	do {								\
		(t)->lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;	\
	} while (0)
#define kp_tab_lock(t)						\
	do {								\
		local_irq_save(flags);					\
		arch_spin_lock(&(t)->lock);				\
	} while (0)
#define kp_tab_unlock(t)						\
	do {								\
		arch_spin_unlock(&(t)->lock);				\
		local_irq_restore(flags);				\
	} while (0)

#define MAXBITS         30
#define MAXASIZE        (1 << MAXBITS)


#define NILCONSTANT     {NULL}, KTAP_TNIL
const struct ktap_value ktap_nilobjectv = {NILCONSTANT};
#define ktap_nilobject	(&ktap_nilobjectv)

static const ktap_tnode dummynode_ = {
	{NILCONSTANT}, /* value */
	{NULL, {NILCONSTANT}}, /* key */
};

#define gnode(t,i)      (&(t)->node[i])
#define gkey(n)         (&(n)->i_key.tvk)
#define gval(n)         (&(n)->i_val)
#define gnext(n)        ((n)->i_key.next)

#define twoto(x)        (1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))

#define hashpow2(t,n)           (gnode(t, lmod((n), sizenode(t))))

#define hashmod(t,n)		(gnode(t, ((n) % ((sizenode(t)-1)|1))))

#define hashstr(t,str)          hashpow2(t, (str)->tsv.hash)
#define hashboolean(t,p)        hashpow2(t, p)
#define hashnum(t, n)		hashmod(t, (unsigned int)n)
#define hashpointer(t,p)	hashmod(t, (unsigned long)(p))

#define dummynode	(&dummynode_)
#define isdummy(n)	((n) == dummynode)

static int ceillog2(unsigned int x)
{
	static const u8 log_2[256] = {
	0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
	6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
	};

	int l = 0;

	x--;
	while (x >= 256) { l += 8; x >>= 8; }
	return l + log_2[x];
}

static inline ktap_stat_data *_read_sd(const ktap_value *v,
				    ktap_tnode *hnode, ktap_stat_data *hsd)
{
	ktap_tnode *node = container_of(v, ktap_tnode, i_val);
	return hsd + (node - hnode);
}

static inline ktap_stat_data *read_sd(ktap_tab *t, const ktap_value *v)
{
	if (v >= &t->array[0] && v < &t->array[t->sizearray])
		return &t->sd_arr[v - &t->array[0]];
	else
		return _read_sd(v, t->node, t->sd_rec);
}

static void update_array_sd(ktap_tab *t)
{
	int i;

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (!is_statdata(v))
			continue;

		set_statdata(v, &t->sd_arr[i]);
	}
}

static void table_free_array(ktap_state *ks, ktap_tab *t)
{
	if (t->sizearray > 0) {
		vfree(t->array);
		vfree(t->sd_arr);
		t->array = NULL;
		t->sd_arr = NULL;
		t->sizearray = 0;
	}
}

static int table_init_array(ktap_state *ks, ktap_tab *t, int size)
{
	if (size == 0)
		return 0;

	t->array = vzalloc(size * sizeof(ktap_value));
	if (!t->array)
		return -ENOMEM;

	if (t->with_stats) {
		t->sd_arr = vzalloc(size * sizeof(ktap_stat_data));
		if (!t->sd_arr) {
			vfree(t->array);
			return -ENOMEM;
		}
		update_array_sd(t);
	}

	t->sizearray = size;
	return 0;
}

static void table_free_record(ktap_state *ks, ktap_tab *t)
{
	if (!isdummy(t->node)) {
		vfree(t->node);
		vfree(t->sd_rec);
		t->node = (ktap_tnode *)dummynode;
		t->sd_rec = NULL;
	}
}

static int table_init_record(ktap_state *ks, ktap_tab *t, int size)
{
	int lsize;

	if (size == 0) {  /* no elements to hash part? */
		t->node = (ktap_tnode *)dummynode;  /* use common `dummynode' */
		lsize = 0;
	} else {
		lsize = ceillog2(size);
		if (lsize > MAXBITS)
			return -EINVAL;

		size = twoto(lsize);
		t->node = vzalloc(size * sizeof(ktap_tnode));
		if (!t->node)
			return -ENOMEM;

		if (t->with_stats) {
			t->sd_rec = vzalloc(size * sizeof(ktap_stat_data));
			if (!t->sd_rec) {
				vfree(t->node);
				t->node = (ktap_tnode *)dummynode;
				return -ENOMEM;
			}
		}
	}

	t->lsizenode = (u8)lsize;
	t->lastfree = gnode(t, size);  /* all positions are free */

	return 0;
}


#define TABLE_NARR_ENTRIES	256 /* PAGE_SIZE / sizeof(ktap_value) */
#define TABLE_NREC_ENTRIES	2048 /* (PAGE_SIZE * 20) / sizeof(ktap_tnode)*/

static
ktap_tab *tab_new_withstat(ktap_state *ks, int narr, int nrec, int with_stat)
{
	int size;

	ktap_tab *t = &kp_newobject(ks, KTAP_TTABLE, sizeof(ktap_tab),
				      NULL)->h;
	t->array = NULL;
	t->sizearray = 0;
	t->node = (ktap_tnode *)dummynode;
	t->lsizenode = 0;
	t->lastfree = t->node;  /* all positions are free */
	t->gclist = NULL;
	t->with_stats = with_stat;
	t->sd_arr = NULL;
	t->sd_rec = NULL;
	t->sorted = NULL;

	kp_tab_lock_init(t);

	if (narr == 0 && nrec == 0) {
		narr = TABLE_NARR_ENTRIES;
		nrec = TABLE_NREC_ENTRIES;
	}

	kp_verbose_printf(ks, "table new, narr: %d, nrec: %d\n", narr, nrec);

	if (table_init_array(ks, t, narr)) {
		kp_error(ks, "cannot allocate %d narr for table\n", narr);
		return NULL;
	}

	if (table_init_record(ks, t, nrec)) {
		table_free_array(ks, t);
		kp_error(ks, "cannot allocate %d nrec for table\n", nrec);
		return NULL;
	}

	size = t->sizearray + sizenode(t);
	t->sorted = vzalloc(size * sizeof(ktap_tnode));
	if (!t->sorted) {
		table_free_array(ks, t);
		table_free_record(ks, t);
		kp_error(ks, "cannot allocate %d bytes memory for table sort\n",
				size * sizeof(ktap_tnode));
		return NULL;
	}

	return t;
}

ktap_tab *kp_tab_new(ktap_state *ks, int narr, int nrec)
{
	return tab_new_withstat(ks, narr, nrec, 0);
}

static const ktap_value *table_getint(ktap_tab *t, int key)
{
	ktap_tnode *n;

	if ((unsigned int)(key - 1) < (unsigned int)t->sizearray)
		return &t->array[key - 1];

	n = hashnum(t, key);
	do {
		if (is_number(gkey(n)) && nvalue(gkey(n)) == key)
			return gval(n);
		else
			n = gnext(n);
	} while (n);

	return ktap_nilobject;
}

const ktap_value *kp_tab_getint(ktap_tab *t, int key)
{
	const ktap_value *val;
	unsigned long flags;

	kp_tab_lock(t);
	val = table_getint(t, key);
	kp_tab_unlock(t);

	return val;
}

static ktap_tnode *mainposition (const ktap_tab *t, const ktap_value *key)
{
	switch (ttype(key)) {
	case KTAP_TNUMBER:
		return hashnum(t, nvalue(key));
	case KTAP_TLNGSTR: {
		ktap_string *s = rawtsvalue(key);
		if (s->tsv.extra == 0) {  /* no hash? */
			s->tsv.hash = kp_string_hash(getstr(s), s->tsv.len,
						     s->tsv.hash);
			s->tsv.extra = 1;  /* now it has its hash */
		}
		return hashstr(t, rawtsvalue(key));
		}
	case KTAP_TSHRSTR:
		return hashstr(t, rawtsvalue(key));
	case KTAP_TBOOLEAN:
		return hashboolean(t, bvalue(key));
	case KTAP_TLIGHTUSERDATA:
		return hashpointer(t, pvalue(key));
	case KTAP_TCFUNCTION:
		return hashpointer(t, fvalue(key));
	case KTAP_TBTRACE: {
		/* use first entry as hash key, cannot use gcvalue as key */
		unsigned long *entries = (unsigned long *)(btvalue(key) + 1);
		return hashpointer(t, entries[0]);
		}
	default:
		return hashpointer(t, gcvalue(key));
	}
}

static int arrayindex(const ktap_value *key)
{
	if (is_number(key)) {
		ktap_number n = nvalue(key);
		int k = (int)n;
		if ((ktap_number)k == n)
			return k;
	}

	/* `key' did not match some condition */
	return -1;
}

/*
 * returns the index of a `key' for table traversals. First goes all
 * elements in the array part, then elements in the hash part. The
 * beginning of a traversal is signaled by -1.
 */
static int findindex(ktap_state *ks, ktap_tab *t, StkId key)
{
	int i;

	if (is_nil(key))
		return -1;  /* first iteration */

	i = arrayindex(key);
	if (i > 0 && i <= t->sizearray)  /* is `key' inside array part? */
		return i - 1;  /* yes; that's the index (corrected to C) */
	else {
		ktap_tnode *n = mainposition(t, key);
		for (;;) {  /* check whether `key' is somewhere in the chain */
			/* key may be dead already, but it is ok to use it in `next' */
			if (kp_equalobjv(ks, gkey(n), key)) {
				i = n - gnode(t, 0);  /* key index in hash table */
				/* hash elements are numbered after array ones */
				return i + t->sizearray;
			} else
				n = gnext(n);

			if (n == NULL)
				/* key not found */
				kp_error(ks, "invalid table key to next");
		}
	}
}

int kp_tab_next(ktap_state *ks, ktap_tab *t, StkId key)
{
	unsigned long flags;
	int i;

	kp_tab_lock(t);

	i = findindex(ks, t, key);  /* find original element */

	for (i++; i < t->sizearray; i++) {  /* try first array part */
	        if (!is_nil(&t->array[i])) {  /* a non-nil value? */
			set_number(key, i+1);
			set_obj(key+1, &t->array[i]);
			kp_tab_unlock(t);
			return 1;
		}
	}

	for (i -= t->sizearray; i < sizenode(t); i++) {  /* then hash part */
		if (!is_nil(gval(gnode(t, i)))) {  /* a non-nil value? */
			set_obj(key, gkey(gnode(t, i)));
			set_obj(key+1, gval(gnode(t, i)));
			kp_tab_unlock(t);
			return 1;
		}
	}

	kp_tab_unlock(t);
	return 0;  /* no more elements */
}

int kp_tab_sort_next(ktap_state *ks, ktap_tab *t, StkId key)
{
	ktap_tnode *node = t->sort_head;
	unsigned long flags;

	kp_tab_lock(t);

	if (is_nil(key)) {
		/* first iteration */
		set_obj(key, gkey(node));
		set_obj(key + 1, gval(node));
		kp_tab_unlock(t);
		return 1;
	}

	while (node && !is_nil(gval(node))) {
		if (kp_equalobjv(ks, gkey(node), key)) {
			node = gnext(node);
			if (!node)
				goto out;

			set_obj(key, gkey(node));
			set_obj(key + 1, gval(node));
			kp_tab_unlock(t);
			return 1;
		}
		node = gnext(node);
	}

 out:
	kp_tab_unlock(t);
	return 0;  /* no more elements */
}


static int default_compare(ktap_state *ks, ktap_closure *cmp_func,
				ktap_value *v1, ktap_value *v2)
{
	if (is_number(v1))
		return nvalue(v1) > nvalue(v2);
	else if (is_statdata(v1))
		return sdvalue(v1)->count > sdvalue(v2)->count;

	return 0;
}

static int closure_compare(ktap_state *ks, ktap_closure *cmp_func,
				ktap_value *v1, ktap_value *v2)
{
	ktap_value *func;
	int res;

	func = ks->top;
	set_closure(ks->top++, cmp_func);
	set_obj(ks->top++, v1);
	set_obj(ks->top++, v2);

	kp_call(ks, func, 1);

	res = !is_false(ks->top - 1);

	ks->top = func; /* restore ks->top */

	return res;
}

static void insert_sorted_list(ktap_state *ks, ktap_tab *t,
				ktap_closure *cmp_func,
				ktap_value *key, ktap_value *val)
{
	ktap_tnode *node = t->sort_head;
	ktap_tnode *newnode, *prevnode = NULL;
	int (*compare)(ktap_state *ks, ktap_closure *cmp_func,
			ktap_value *v1, ktap_value *v2);
	int i = 0;

	if (is_nil(gval(node))) {
		*gkey(node) = *key;
		*gval(node) = *val;
		return;
	}

	if (!cmp_func)
		compare = default_compare;
	else
		compare = closure_compare;

	while (node) {
		if (compare(ks, cmp_func, gval(node), val)) {
			prevnode = node;
			node = gnext(node);
			continue;
		} else
			break;
	}

	/* find free position */
	while (!is_nil(gval(&t->sorted[i]))) {
		i++;
	}

	newnode = &t->sorted[i];
	*gkey(newnode) = *key;
	*gval(newnode) = *val;
	gnext(newnode) = node;
	if (prevnode)
		gnext(prevnode) = newnode;
	else
		t->sort_head = newnode;
}

void kp_tab_sort(ktap_state *ks, ktap_tab *t, ktap_closure *cmp_func)
{
	unsigned long flags;
	int i;

	kp_tab_lock(t);

	t->sort_head = t->sorted;

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];
		ktap_value key;

	        if (!is_nil(v)) {
			set_number(&key, i + 1);
			insert_sorted_list(ks, t, cmp_func, &key, v);
		}
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *node = &t->node[i];

		if (is_nil(gkey(node)))
			continue;

		insert_sorted_list(ks, t, cmp_func, gkey(node), gval(node));
	}

	kp_tab_unlock(t);
}

static ktap_tnode *getfreepos(ktap_tab *t)
{
	while (t->lastfree > t->node) {
		t->lastfree--;
		if (is_nil(gkey(t->lastfree)))
			return t->lastfree;
	}
	return NULL;  /* could not find a free place */
}

static ktap_value *table_newkey(ktap_state *ks, ktap_tab *t,
				const ktap_value *key)
{
	ktap_tnode *mp;
	ktap_value newkey;

	mp = mainposition(t, key);
	if (!is_nil(gval(mp)) || isdummy(mp)) {  /* main position is taken? */
		ktap_tnode *othern;
		ktap_tnode *n = getfreepos(t);  /* get a free place */
		if (unlikely(n == NULL)) {  /* cannot find a free place? */
			kp_error(ks, "table overflow, please enlarge entries for this table\n");
			return NULL;
		}

		othern = mainposition(t, gkey(mp));
		if (othern != mp) {
			/* is colliding node out of its main position? */

			/* move colliding node into free position */
			while (gnext(othern) != mp)
				othern = gnext(othern);  /* find previous */

			/* redo the chain with `n' in place of `mp' */
			gnext(othern) = n;

			/* copy colliding node into free pos */
			*n = *mp;

			if (t->with_stats) {
				ktap_stat_data *sd = read_sd(t, gval(n));
				*sd = *read_sd(t, gval(mp));
				set_statdata(gval(n), sd);
			}

			gnext(mp) = NULL;  /* now `mp' is free */
			set_nil(gval(mp));
		} else {
			/* colliding node is in its own main position */

			/* new node will go into free position */
			gnext(n) = gnext(mp);  /* chain new position */
			gnext(mp) = n;
			mp = n;
		}
	}

	/* special handling for cloneable object, maily for btrace object */
	if (is_needclone(key))
		kp_objclone(ks, key, &newkey, &t->gclist);
	else
		newkey = *key;

	set_obj(gkey(mp), &newkey);
	return gval(mp);
}


/*
 * search function for short strings
 */
static const ktap_value *table_getstr(ktap_tab *t, ktap_string *key)
{
	ktap_tnode *n = hashstr(t, key);

	do {  /* check whether `key' is somewhere in the chain */
		if (is_shrstring(gkey(n)) && eqshrstr(rawtsvalue(gkey(n)),
								key))
			return gval(n);  /* that's it */
		else
			n = gnext(n);
	} while (n);

	return ktap_nilobject;
}


/*
 * main search function
 */
static const ktap_value *table_get(ktap_tab *t, const ktap_value *key)
{
	switch (ttype(key)) {
	case KTAP_TNIL:
		return ktap_nilobject;
	case KTAP_TSHRSTR:
		return table_getstr(t, rawtsvalue(key));
	case KTAP_TNUMBER: {
		ktap_number n = nvalue(key);
		int k = (int)n;
		if ((ktap_number)k == nvalue(key)) /* index is int? */
			return table_getint(t, k);  /* use specialized version */
		/* else go through */
	}
	default: {
		ktap_tnode *n = mainposition(t, key);
		do {  /* check whether `key' is somewhere in the chain */
			if (rawequalobj(gkey(n), key))
				return gval(n);  /* that's it */
			else
				n = gnext(n);
		} while (n);

		return ktap_nilobject;
	}
	}
}

const ktap_value *kp_tab_get(ktap_tab *t, const ktap_value *key)
{
	const ktap_value *val;
	unsigned long flags;

	kp_tab_lock(t);
	val = table_get(t, key);
	kp_tab_unlock(t);

	return val;
}

static ktap_value *table_set(ktap_state *ks, ktap_tab *t,
			     const ktap_value *key)
{
	const ktap_value *p = table_get(t, key);

	if (p != ktap_nilobject)
		return (ktap_value *)p;
	else
		return table_newkey(ks, t, key);
}

void kp_tab_setvalue(ktap_state *ks, ktap_tab *t,
		       const ktap_value *key, ktap_value *val)
{
	ktap_value *v;
	unsigned long flags;

	if (is_nil(key)) {
		kp_printf(ks, "table index is nil\n");
		kp_exit(ks);
		return;
	}

	kp_tab_lock(t);
	v = table_set(ks, t, key);
	if (likely(v))
		set_obj(v, val);

	kp_tab_unlock(t);
}

static void table_setint(ktap_state *ks, ktap_tab *t, int key, ktap_value *v)
{
	const ktap_value *p;
	ktap_value *cell;

	p = table_getint(t, key);

	if (p != ktap_nilobject)
		cell = (ktap_value *)p;
	else {
		ktap_value k;
		set_number(&k, key);
		cell = table_newkey(ks, t, &k);
		if (unlikely(!cell))
			return;
	}

	set_obj(cell, v);
}

void kp_tab_setint(ktap_state *ks, ktap_tab *t, int key, ktap_value *val)
{
	unsigned long flags;

	kp_tab_lock(t);
	table_setint(ks, t, key, val);
	kp_tab_unlock(t);
}

void kp_tab_atomic_inc(ktap_state *ks, ktap_tab *t, ktap_value *key, int n)
{
	unsigned long flags;
	ktap_value *v;

	if (is_nil(key)) {
		kp_printf(ks, "table index is nil\n");
		kp_exit(ks);
		return;
	}

	kp_tab_lock(t);

	v = table_set(ks, t, key);
	if (unlikely(!v))
		goto out;

	if (is_nil(v)) {
		set_number(v, n);
	} else
		set_number(v, nvalue(v) + n);

 out:
	kp_tab_unlock(t);
}

int kp_tab_length(ktap_state *ks, ktap_tab *t)
{
	unsigned long flags;
	int i, len = 0;

	kp_tab_lock(t);

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (is_nil(v))
			continue;
		len++;
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *n = &t->node[i];

		if (is_nil(gkey(n)))
			continue;

		len++;
	}
	
	kp_tab_unlock(t);
	return len;
}

void kp_tab_free(ktap_state *ks, ktap_tab *t)
{
	table_free_array(ks, t);
	table_free_record(ks, t);

	if (t->sorted)
		vfree(t->sorted);

	kp_free_gclist(ks, t->gclist);
	kp_free(ks, t);
}

void kp_tab_dump(ktap_state *ks, ktap_tab *t)
{
	int i;

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (is_nil(v))
			continue;

		kp_printf(ks, "%d:\t", i + 1);
		kp_showobj(ks, v);
		kp_puts(ks, "\n");
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *n = &t->node[i];

		if (is_nil(gkey(n)))
			continue;

		kp_showobj(ks, gkey(n));
		kp_puts(ks, ":\t");
		kp_showobj(ks, gval(n));
		kp_puts(ks, "\n");
	}
}

/*
 * table-clear only set nil of all elements, not free t->array and nodes.
 * we assume user will reuse table soon after clear table, so reserve array
 * and nodes will avoid memory allocation when insert key-value again.
 */
void kp_tab_clear(ktap_state *ks, ktap_tab *t)
{
	unsigned long flags;

	kp_tab_lock(t);

	memset(t->array, 0, t->sizearray * sizeof(ktap_value));
	memset(t->node, 0, sizenode(t) * sizeof(ktap_tnode));

	kp_tab_unlock(t);
}

static void string_convert(char *output, const char *input)
{
	if (strlen(input) > 32) {
		strncpy(output, input, 32-4);
		memset(output + 32-4, '.', 3);
	} else
		memcpy(output, input, strlen(input));
}

/* todo: make histdump to be faster */

/* histogram: key should be number or string, value must be number */
static void table_histdump(ktap_state *ks, ktap_tab *t, int shownums)
{
	int total = 0, is_kernel_address = 0;
	char dist_str[40];
	ktap_tnode *node;

	kp_tab_sort(ks, t, NULL);

	for (node = t->sort_head; node; node = gnext(node)) {
		ktap_value *val = gval(node);
		if (is_number(val))
			total += nvalue(val);
		else if (is_statdata(val))
			total += sdvalue(val)->count;
		else {
			kp_error(ks, "table histogram only handle "
				     " (key: string/number val: number)\n");
			return;
		}
	}

	/* check the first key is a kernel text symbol or not */
	if (is_number(gkey(t->sort_head))) {
		char str[KSYM_SYMBOL_LEN];

		SPRINT_SYMBOL(str, nvalue(gkey(t->sort_head)));
		if (str[0] != '0' || str[1] != 'x')
			is_kernel_address = 1;
	}

	dist_str[sizeof(dist_str) - 1] = '\0';

	for (node = t->sort_head; node; node = gnext(node)) {
		ktap_value *key = gkey(node);
		ktap_value *val = gval(node);
		int num = 0, ratio;

		if (!--shownums)
			break;

		if (is_number(val))
			num = nvalue(val);
		else if (is_statdata(val))
			num = sdvalue(val)->count;

		memset(dist_str, ' ', sizeof(dist_str) - 1);
		ratio = (num * (sizeof(dist_str) - 1)) / total;
		memset(dist_str, '@', ratio);

		if (is_string(key)) {
			char buf[32 + 1] = {0};

			string_convert(buf, svalue(key));
			kp_printf(ks, "%32s |%s%-7d\n", buf, dist_str, num);
		} else if (is_number(key)) {
			char str[KSYM_SYMBOL_LEN];
			char buf[32 + 1] = {0};

			if (is_kernel_address) {
				/* suppose it's a symbol, fix it in future */
				SPRINT_SYMBOL(str, nvalue(key));
				string_convert(buf, str);
				kp_printf(ks, "%32s |%s%-7d\n", buf, dist_str,
						num);
			} else {
				kp_printf(ks, "%32d |%s%-7d\n", nvalue(key),
						dist_str, num);
			}
		}
	}

	if (!shownums && node)
		kp_printf(ks, "%32s |\n", "...");
}

#define HISTOGRAM_DEFAULT_TOP_NUM	20

#define DISTRIBUTION_STR "------------- Distribution -------------"
void kp_tab_histogram(ktap_state *ks, ktap_tab *t)
{
	kp_printf(ks, "%32s%s%s\n", "value ", DISTRIBUTION_STR, " count");
	table_histdump(ks, t, HISTOGRAM_DEFAULT_TOP_NUM);
}

/*
 * Parallel Table
 */

void kp_statdata_dump(ktap_state *ks, ktap_stat_data *sd)
{
	kp_printf(ks, "[count: %6d sum: %6d max: %6d min: %6d avg: %6d]",
		sd->count, sd->sum, sd->max, sd->min, sd->sum/sd->count);
}

static void statdata_add(ktap_stat_data *sd1, ktap_stat_data *sd2)
{
	sd2->count += sd1->count;
	sd2->sum += sd1->sum;
	if (sd1->max > sd2->max)
		sd2->max = sd1->max;
	if (sd1->min < sd2->min)
		sd2->min = sd1->min;
}

static void merge_table(ktap_state *ks, ktap_tab *t1, ktap_tab *t2)
{
	unsigned long flags;
	ktap_value *newv;
	ktap_value n;
	int i;

	kp_tab_lock(t1);
	kp_tab_lock(t2);

	for (i = 0; i < t1->sizearray; i++) {
		ktap_value *v = &t1->array[i];
		ktap_stat_data *sd;

		if (is_nil(v))
			continue;

		set_number(&n, i);

		newv = table_set(ks, t2, &n);
		if (unlikely(!newv))
			break;
		sd = read_sd(t2, newv);
		if (is_nil(newv)) {
			*sd = *read_sd(t1, v);
			set_statdata(newv, sd);
		} else
			statdata_add(read_sd(t1, v), sd);
	}

	for (i = 0; i < sizenode(t1); i++) {
		ktap_tnode *node = &t1->node[i];

		if (is_nil(gkey(node)))
			continue;

		newv = table_set(ks, t2, gkey(node));
		if (unlikely(!newv))
			break;
		if (is_nil(newv)) {
			*read_sd(t2, newv) = *read_sd(t1, gval(node));
			set_statdata(newv, read_sd(t2, newv));
		} else
			statdata_add(read_sd(t1, gval(node)),
				     read_sd(t2, newv));
	}

	kp_tab_unlock(t2);
	kp_tab_unlock(t1);
}

ktap_tab *kp_ptab_synthesis(ktap_state *ks, ktap_ptab *ph)
{
	ktap_tab *agg;
	int cpu;

	agg = ph->agg;

	/* clear the table content before store new elements */
	kp_tab_clear(ks, agg);

	for_each_possible_cpu(cpu) {
		ktap_tab **t = per_cpu_ptr(ph->tbl, cpu);
		merge_table(ks, *t, agg);
	}

	return agg;
}

void kp_ptab_dump(ktap_state *ks, ktap_ptab *ph)
{
	kp_tab_dump(ks, kp_ptab_synthesis(ks, ph));
}

ktap_ptab *kp_ptab_new(ktap_state *ks, int narr, int nrec)
{
	ktap_ptab *ph;
	int cpu;

	ph = &kp_newobject(ks, KTAP_TPTABLE, sizeof(ktap_ptab),
			NULL)->ph;
	ph->tbl = alloc_percpu(ktap_tab *);

	for_each_possible_cpu(cpu) {
		ktap_tab **t = per_cpu_ptr(ph->tbl, cpu);
		*t = tab_new_withstat(ks, narr, nrec, 1);
	}

	ph->agg = tab_new_withstat(ks, narr, nrec * (cpu - 1), 1);
	return ph;
}

void kp_ptab_free(ktap_state *ks, ktap_ptab *ph)
{
	free_percpu(ph->tbl);
	kp_free(ks, ph);
}

void kp_ptab_set(ktap_state *ks, ktap_ptab *ph,
				 ktap_value *key, ktap_value *val)
{
	ktap_tab *t = *__this_cpu_ptr(ph->tbl);
	unsigned long flags;
	ktap_value *v;
	ktap_stat_data *sd;
	int aggval;

	if (unlikely(!is_number(val))) {
		kp_error(ks, "add non number value to aggregation table\n");
		return;
	}

	aggval = nvalue(val);

	kp_tab_lock(t);

	v = table_set(ks, t, key);
	/* table maybe overflow, need to check */
	if (unlikely(!v))
		goto out;

	sd = read_sd(t, v);

	if (is_nil(v)) {
		sd->count = 1;
		sd->sum = sd->min = sd->max = aggval;
		set_statdata(v, sd);
		goto out;
	}

	sd->count++;
	sd->sum += aggval;
	if (aggval > sd->max)
		sd->max = aggval;
	if (aggval < sd->min)
		sd->min = aggval;

 out:
	kp_tab_unlock(t);
}

void kp_ptab_get(ktap_state *ks, ktap_ptab *ph,
				 ktap_value *key, ktap_value *val)
{
	unsigned long flags;
	ktap_stat_data sd, *aggsd;
	const ktap_value *v;
	ktap_value *aggval;
	int cpu;

	sd.count = sd.sum = sd.max = sd.min = -1;

	for_each_possible_cpu(cpu) {
		ktap_tab **t = per_cpu_ptr(ph->tbl, cpu);

		kp_tab_lock(*t);
		v = table_get(*t, key);
		if (is_nil(v)) {
			kp_tab_unlock(*t);
			continue;
		}

		if (sd.count == -1) {
			sd = *read_sd(*t, v);
			kp_tab_unlock(*t);
			continue;
		}

		statdata_add(read_sd(*t, v), &sd);
		kp_tab_unlock(*t);
	}

	if (sd.count == -1) {
		set_nil(val);
		return;
	}

	kp_tab_lock(ph->agg);
	aggval = table_set(ks, ph->agg, key);
	if (unlikely(!aggval))
		goto out;
	aggsd = read_sd(ph->agg, aggval);
	*aggsd = sd;
	set_statdata(aggval, aggsd);
	set_statdata(val, aggsd);
 out:
	kp_tab_unlock(ph->agg);
}

void kp_ptab_histogram(ktap_state *ks, ktap_ptab *ph)
{
	kp_tab_histogram(ks, kp_ptab_synthesis(ks, ph));
}

