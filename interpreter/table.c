/*
 * table.c - ktap table data structure manipulation function
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

#ifdef __KERNEL__
#include "../include/ktap.h"
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/sort.h>
#else
#include "../include/ktap_types.h"

static inline void sort(void *base, size_t num, size_t size,
			int (*cmp_func)(const void *, const void *),
			void (*swap_func)(void *, void *, int size))
{}
#endif


#ifdef __KERNEL__
#define kp_table_lock_init(t)						\
	do {								\
		(t)->lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;	\
	} while (0)
#define kp_table_lock(t)						\
	do {								\
		local_irq_save(flags);					\
		arch_spin_lock(&(t)->lock);				\
	} while (0)
#define kp_table_unlock(t)						\
	do {								\
		arch_spin_unlock(&(t)->lock);				\
		local_irq_restore(flags);				\
	} while (0)

#else
#define kp_table_lock_init(t)
#define kp_table_lock(t)
#define kp_table_unlock(t)
#endif

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

static void table_setint(ktap_state *ks, ktap_table *t, int key, ktap_value *v);
static ktap_value *table_set(ktap_state *ks, ktap_table *t,
			     const ktap_value *key);
static void setnodevector(ktap_state *ks, ktap_table *t, int size);

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

#ifdef __KERNEL__
static inline ktap_stat_data *_read_sd(const ktap_value *v,
				    ktap_tnode *hnode, ktap_stat_data *hsd)
{
	ktap_tnode *node = container_of(v, ktap_tnode, i_val);
	return hsd + (node - hnode);
}

static inline ktap_stat_data *read_sd(ktap_table *t, const ktap_value *v)
{
	if (v >= &t->array[0] && v < &t->array[t->sizearray])
		return &t->sd_arr[v - &t->array[0]];
	else
		return _read_sd(v, t->node, t->sd_rec);
}

#else
static inline ktap_stat_data *_read_sd(const ktap_value *v,
				    ktap_tnode *hnode, ktap_stat_data *hsd)
{
	return NULL;
}

static inline ktap_stat_data *read_sd(ktap_table *t, const ktap_value *v)
{
	return NULL;
}
#endif


ktap_table *kp_table_new(ktap_state *ks)
{
	ktap_table *t = &kp_newobject(ks, KTAP_TTABLE, sizeof(ktap_table),
				      NULL)->h;
	t->flags = (u8)(~0);
	t->array = NULL;
	t->sizearray = 0;
	t->node = (ktap_tnode *)dummynode;
	t->gclist = NULL;
	t->with_stats = 0;
	t->sd_arr = NULL;
	t->sd_rec = NULL;
	setnodevector(ks, t, 0);

	t->sorted = NULL;
	t->sort_head = NULL;

	kp_table_lock_init(t);
	return t;
}

static const ktap_value *table_getint(ktap_table *t, int key)
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

const ktap_value *kp_table_getint(ktap_table *t, int key)
{
	const ktap_value *val;
	unsigned long __maybe_unused flags;

	kp_table_lock(t);
	val = table_getint(t, key);
	kp_table_unlock(t);

	return val;
}

static ktap_tnode *mainposition (const ktap_table *t, const ktap_value *key)
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
static int findindex(ktap_state *ks, ktap_table *t, StkId key)
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

int kp_table_next(ktap_state *ks, ktap_table *t, StkId key)
{
	unsigned long __maybe_unused flags;
	int i;

	kp_table_lock(t);

	i = findindex(ks, t, key);  /* find original element */

	for (i++; i < t->sizearray; i++) {  /* try first array part */
	        if (!is_nil(&t->array[i])) {  /* a non-nil value? */
			set_number(key, i+1);
			set_obj(key+1, &t->array[i]);
			kp_table_unlock(t);
			return 1;
		}
	}

	for (i -= t->sizearray; i < sizenode(t); i++) {  /* then hash part */
		if (!is_nil(gval(gnode(t, i)))) {  /* a non-nil value? */
			set_obj(key, gkey(gnode(t, i)));
			set_obj(key+1, gval(gnode(t, i)));
			kp_table_unlock(t);
			return 1;
		}
	}

	kp_table_unlock(t);
	return 0;  /* no more elements */
}

#ifdef __KERNEL__
int kp_table_sort_next(ktap_state *ks, ktap_table *t, StkId key)
{
	unsigned long __maybe_unused flags;
	ktap_tnode *node = t->sort_head;

	kp_table_lock(t);

	if (is_nil(key)) {
		/* first iteration */
		set_obj(key, gkey(node));
		set_obj(key + 1, gval(node));
		kp_table_unlock(t);
		return 1;
	}

	while (node && !is_nil(gval(node))) {
		if (kp_equalobjv(ks, gkey(node), key)) {
			node = gnext(node);
			if (!node)
				goto out;

			set_obj(key, gkey(node));
			set_obj(key + 1, gval(node));
			kp_table_unlock(t);
			return 1;
		}
		node = gnext(node);
	}

 out:
	kp_table_unlock(t);
	return 0;  /* no more elements */
}


static int default_compare(ktap_state *ks, ktap_closure *cmp_func,
				ktap_value *v1, ktap_value *v2)
{
	return nvalue(v1) < nvalue(v2);
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

static void insert_sorted_list(ktap_state *ks, ktap_table *t,
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
		//if (nvalue(gval(node)) < nvalue(val)) {
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

void kp_table_sort(ktap_state *ks, ktap_table *t, ktap_closure *cmp_func)
{
	unsigned long __maybe_unused flags;
	int size = t->sizearray + sizenode(t);
	int i;

	kp_table_lock(t);

	kp_realloc(ks, t->sorted, 0, size, ktap_tnode);
	memset(t->sorted, 0, size * sizeof(ktap_tnode));
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

	kp_table_unlock(t);
}
#endif

static int computesizes (int nums[], int *narray)
{
	int i;
	int twotoi;  /* 2^i */
	int a = 0;  /* number of elements smaller than 2^i */
	int na = 0;  /* number of elements to go to array part */
	int n = 0;  /* optimal size for array part */

	for (i = 0, twotoi = 1; twotoi/2 < *narray; i++, twotoi *= 2) {
		if (nums[i] > 0) {
			a += nums[i];
			/* more than half elements present? */
			if (a > twotoi/2) {
				/* optimal size (till now) */
				n = twotoi;
				/*
				 * all elements smaller than n will go to
				 * array part
				 */
				na = a;
			}
		}
		if (a == *narray)
			break;  /* all elements already counted */
	}
	*narray = n;
	return na;
}


static int countint(const ktap_value *key, int *nums)
{
	int k = arrayindex(key);

	/* is `key' an appropriate array index? */
	if (0 < k && k <= MAXASIZE) {
		nums[ceillog2(k)]++;  /* count as such */
		return 1;
	} else
		return 0;
}


static int numusearray(const ktap_table *t, int *nums)
{
	int lg;
	int ttlg;  /* 2^lg */
	int ause = 0;  /* summation of `nums' */
	int i = 1;  /* count to traverse all array keys */

	/* for each slice */
	for (lg=0, ttlg=1; lg <= MAXBITS; lg++, ttlg *= 2) {
		int lc = 0;  /* counter */
		int lim = ttlg;

		if (lim > t->sizearray) {
			lim = t->sizearray;  /* adjust upper limit */
			if (i > lim)
				break;  /* no more elements to count */
		}

		/* count elements in range (2^(lg-1), 2^lg] */
		for (; i <= lim; i++) {
			if (!is_nil(&t->array[i-1]))
				lc++;
		}
		nums[lg] += lc;
		ause += lc;
	}
	return ause;
}

static int numusehash(const ktap_table *t, int *nums, int *pnasize)
{
	int totaluse = 0;  /* total number of elements */
	int ause = 0;  /* summation of `nums' */
	int i = sizenode(t);

	while (i--) {
		ktap_tnode *n = &t->node[i];
		if (!is_nil(gval(n))) {
			ause += countint(gkey(n), nums);
			totaluse++;
		}
	}

	*pnasize += ause;
	return totaluse;
}

static void update_array_sd(ktap_table *t)
{
	int i;

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (!is_statdata(v))
			continue;

		set_statdata(v, &t->sd_arr[i]);
	}
}

static void setarrayvector(ktap_state *ks, ktap_table *t, int size)
{
	int i;

	kp_realloc(ks, t->array, t->sizearray, size, ktap_value);
	if (t->with_stats) {
		kp_realloc(ks, t->sd_arr, t->sizearray, size,
				ktap_stat_data);
		update_array_sd(t);
	}

	for (i = t->sizearray; i < size; i++)
		set_nil(&t->array[i]);

	t->sizearray = size;
}

static void setnodevector(ktap_state *ks, ktap_table *t, int size)
{
	int lsize;

	if (size == 0) {  /* no elements to hash part? */
		t->node = (ktap_tnode *)dummynode;  /* use common `dummynode' */
		lsize = 0;
	} else {
		int i;
		lsize = ceillog2(size);
		if (lsize > MAXBITS) {
			kp_error(ks, "table overflow\n");
			return;
		}

		size = twoto(lsize);
		t->node = kp_malloc(ks, size * sizeof(ktap_tnode));
		if (t->with_stats)
			t->sd_rec = kp_malloc(ks, size *
						sizeof(ktap_stat_data));
		for (i = 0; i < size; i++) {
			ktap_tnode *n = gnode(t, i);
			gnext(n) = NULL;
			set_nil(gkey(n));
			set_nil(gval(n));
		}
	}

	t->lsizenode = (u8)lsize;
	t->lastfree = gnode(t, size);  /* all positions are free */
}

static void table_resize(ktap_state *ks, ktap_table *t, int nasize, int nhsize)
{
	int oldasize = t->sizearray;
	int oldhsize = t->lsizenode;
	ktap_tnode *nold = t->node;  /* save old hash */
	ktap_stat_data *sd_rec_old = t->sd_rec;  /* save stat_data */
	int i;

#ifdef __KERNEL__
	kp_verbose_printf(ks, "table resize, nasize: %d, nhsize: %d\n",
				nasize, nhsize);
#endif

	if (nasize > oldasize)  /* array part must grow? */
		setarrayvector(ks, t, nasize);

	/* create new hash part with appropriate size */
	setnodevector(ks, t, nhsize);

	if (nasize < oldasize) {  /* array part must shrink? */
		t->sizearray = nasize;
		/* re-insert elements from vanishing slice */
		for (i = nasize; i < oldasize; i++) {
			if (!is_nil(&t->array[i])) {
				ktap_value *v;
				v = (ktap_value *)table_getint(t, i + 1);
				set_obj(v, &t->array[i]);

				if (t->with_stats) {
					*read_sd(t, v) = t->sd_arr[i];
					set_statdata(v, read_sd(t, v));
				}
			}
		}

		/* shrink array */
		kp_realloc(ks, t->array, oldasize, nasize, ktap_value);
		if (t->with_stats) {
			kp_realloc(ks, t->sd_arr, oldasize, nasize,
					ktap_stat_data);
			update_array_sd(t);
		}
	}

	/* re-insert elements from hash part */
	for (i = twoto(oldhsize) - 1; i >= 0; i--) {
		ktap_tnode *old = nold + i;
		if (!is_nil(gval(old))) {
			ktap_value *v = table_set(ks, t, gkey(old));
			/*
			 * doesn't need barrier/invalidate cache, as entry was
			 * already present in the table
			 */
			set_obj(v, gval(old));

			if (t->with_stats) {
				ktap_stat_data *sd;

				sd = read_sd(t, v);
				*sd = *_read_sd(gval(old), nold, sd_rec_old);
				set_statdata(v, sd);
			}
		}
	}

	if (!isdummy(nold)) {
		kp_free(ks, nold); /* free old array */
		kp_free(ks, sd_rec_old);
	}
}

void kp_table_resize(ktap_state *ks, ktap_table *t, int nasize, int nhsize)
{
	unsigned long __maybe_unused flags;

	kp_table_lock(t);
	table_resize(ks, t, nasize, nhsize);
	kp_table_unlock(t);
}

void kp_table_resizearray(ktap_state *ks, ktap_table *t, int nasize)
{
	unsigned long __maybe_unused flags;
	int nsize;

	kp_table_lock(t);

	nsize = isdummy(t->node) ? 0 : sizenode(t);
	table_resize(ks, t, nasize, nsize);

	kp_table_unlock(t);
}

static void rehash(ktap_state *ks, ktap_table *t, const ktap_value *ek)
{
	int nasize, na;
	/* nums[i] = number of keys with 2^(i-1) < k <= 2^i */
	int nums[MAXBITS+1];
	int i;
	int totaluse;

	for (i = 0; i <= MAXBITS; i++)
		nums[i] = 0;  /* reset counts */

	nasize = numusearray(t, nums);  /* count keys in array part */
	totaluse = nasize;  /* all those keys are integer keys */
	totaluse += numusehash(t, nums, &nasize);  /* count keys in hash part */
	/* count extra key */
	nasize += countint(ek, nums);
	totaluse++;
	/* compute new size for array part */
	na = computesizes(nums, &nasize);
	/* resize the table to new computed sizes */
	table_resize(ks, t, nasize, totaluse - na);
}


static ktap_tnode *getfreepos(ktap_table *t)
{
	while (t->lastfree > t->node) {
		t->lastfree--;
		if (is_nil(gkey(t->lastfree)))
			return t->lastfree;
	}
	return NULL;  /* could not find a free place */
}


static ktap_value *table_newkey(ktap_state *ks, ktap_table *t,
				const ktap_value *key)
{
	ktap_tnode *mp;
	ktap_value newkey;

	mp = mainposition(t, key);
	if (!is_nil(gval(mp)) || isdummy(mp)) {  /* main position is taken? */
		ktap_tnode *othern;
		ktap_tnode *n = getfreepos(t);  /* get a free place */
		if (n == NULL) {  /* cannot find a free place? */
			rehash(ks, t, key);  /* grow table */
			/* insert key into grown table */
			return table_set(ks, t, key);
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
static const ktap_value *table_getstr(ktap_table *t, ktap_string *key)
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
static const ktap_value *table_get(ktap_table *t, const ktap_value *key)
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

const ktap_value *kp_table_get(ktap_table *t, const ktap_value *key)
{
	const ktap_value *val;
	unsigned long __maybe_unused flags;

	kp_table_lock(t);
	val = table_get(t, key);
	kp_table_unlock(t);

	return val;
}

static ktap_value *table_set(ktap_state *ks, ktap_table *t,
			     const ktap_value *key)
{
	const ktap_value *p = table_get(t, key);

	if (p != ktap_nilobject)
		return (ktap_value *)p;
	else
		return table_newkey(ks, t, key);
}

void kp_table_setvalue(ktap_state *ks, ktap_table *t,
		       const ktap_value *key, ktap_value *val)
{
	unsigned long __maybe_unused flags;

	if (is_nil(key)) {
		kp_printf(ks, "table index is nil\n");
		kp_exit(ks);
		return;
	}

	kp_table_lock(t);
	set_obj(table_set(ks, t, key), val);
	kp_table_unlock(t);
}

static void table_setint(ktap_state *ks, ktap_table *t, int key, ktap_value *v)
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
	}

	set_obj(cell, v);
}

void kp_table_setint(ktap_state *ks, ktap_table *t, int key, ktap_value *val)
{
	unsigned long __maybe_unused flags;

	kp_table_lock(t);
	table_setint(ks, t, key, val);
	kp_table_unlock(t);
}

void kp_table_atomic_inc(ktap_state *ks, ktap_table *t, ktap_value *key, int n)
{
	unsigned long __maybe_unused flags;
	ktap_value *v;

	if (is_nil(key)) {
		kp_printf(ks, "table index is nil\n");
		kp_exit(ks);
		return;
	}

	kp_table_lock(t);

	v = table_set(ks, t, key);
	if (is_nil(v)) {
		set_number(v, n);
	} else
		set_number(v, nvalue(v) + n);

	kp_table_unlock(t);
}

int kp_table_length(ktap_state *ks, ktap_table *t)
{
	unsigned long __maybe_unused flags;
	int i, len = 0;

	kp_table_lock(t);

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
	
	kp_table_unlock(t);
	return len;
}

void kp_table_free(ktap_state *ks, ktap_table *t)
{
	if (t->sizearray > 0) {
		kp_free(ks, t->array);
		kp_free(ks, t->sd_arr);
	}

	if (!isdummy(t->node)) {
		kp_free(ks, t->node);
		kp_free(ks, t->sd_rec);
	}

	kp_free(ks, t->sorted);
	kp_free_gclist(ks, t->gclist);
	kp_free(ks, t);
}

void kp_table_dump(ktap_state *ks, ktap_table *t)
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
void kp_table_clear(ktap_state *ks, ktap_table *t)
{
	unsigned long __maybe_unused flags;

	kp_table_lock(t);

	memset(t->array, 0, t->sizearray * sizeof(ktap_value));
	memset(t->node, 0, sizenode(t) * sizeof(ktap_tnode));

	kp_table_unlock(t);
}

#ifdef __KERNEL__
static void string_convert(char *output, const char *input)
{
	if (strlen(input) > 32) {
		strncpy(output, input, 32-4);
		memset(output + 32-4, '.', 3);
	} else
		memcpy(output, input, strlen(input));
}

struct table_hist_record {
	ktap_value key;
	ktap_value val;
};

static int hist_record_cmp(const void *r1, const void *r2)
{
	const struct table_hist_record *i = r1;
	const struct table_hist_record *j = r2;

	if ((nvalue(&i->val) == nvalue(&j->val))) {
		return 0;
	} else if ((nvalue(&i->val) < nvalue(&j->val))) {
		return 1;
	} else
		return -1;
}

/* todo: make histdump to be faster */

/* histogram: key should be number or string, value must be number */
static void table_histdump(ktap_state *ks, ktap_table *t, int shownums)
{
	struct table_hist_record *thr;
	unsigned long __maybe_unused flags;
	char dist_str[40];
	int i, ratio, total = 0, count = 0, top_num, is_kernel_address = 0;
	int size, num;

	size = sizeof(*thr) * (t->sizearray + sizenode(t));
	thr = kp_malloc(ks, size);
	if (!thr) {
		kp_error(ks, "Cannot allocate %d of histogram memory", size);
		return;
	}

	kp_table_lock(t);

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (is_nil(v))
			continue;

		if (is_number(v))
			num = nvalue(v);
		else if (is_statdata(v))
			num = sdvalue(v)->count;
		else {
			kp_table_unlock(t);
			goto error;
		}

		set_number(&thr[count].key, i + 1);
		set_number(&thr[count].val, num);
		count++;
		total += num;
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *n = &t->node[i];
		ktap_value *v = gval(n);

		if (is_nil(gkey(n)))
			continue;

		if (is_number(v))
			num = nvalue(v);
		else if (is_statdata(v))
			num = sdvalue(v)->count;
		else {
			kp_table_unlock(t);
			goto error;
		}

		set_obj(&thr[count].key, gkey(n));
		set_number(&thr[count].val, num);
		count++;
		total += num;
	}

	kp_table_unlock(t);

	sort(thr, count, sizeof(struct table_hist_record),
	     hist_record_cmp, NULL);

	dist_str[sizeof(dist_str) - 1] = '\0';

	/* check the first key is a kernel text symbol or not */
	if (is_number(&thr[0].key)) {
		char str[KSYM_SYMBOL_LEN];

		SPRINT_SYMBOL(str, nvalue(&thr[0].key));
		if (str[0] != '0' || str[1] != 'x')
			is_kernel_address = 1;
	}

	top_num = min(shownums, count);
	for (i = 0; i < top_num; i++) {
		ktap_value *key = &thr[i].key;
		ktap_value *val = &thr[i].val;

		memset(dist_str, ' ', sizeof(dist_str) - 1);
		ratio = (nvalue(val) * (sizeof(dist_str) - 1)) / total;
		memset(dist_str, '@', ratio);

		if (is_string(key)) {
			char buf[32 + 1] = {0};

			string_convert(buf, svalue(key));
			kp_printf(ks, "%32s |%s%-7d\n", buf, dist_str,
				      nvalue(val));
		} else if (is_number(key)) {
			char str[KSYM_SYMBOL_LEN];
			char buf[32 + 1] = {0};

			if (is_kernel_address) {
				/* suppose it's a symbol, fix it in future */
				SPRINT_SYMBOL(str, nvalue(key));
				string_convert(buf, str);
				kp_printf(ks, "%32s |%s%-7d\n", buf, dist_str,
						nvalue(val));
			} else {
				kp_printf(ks, "%32d |%s%-7d\n", nvalue(key),
						dist_str, nvalue(val));
			}
		}
	}

	if (count > shownums)
		kp_printf(ks, "%32s |\n", "...");

	goto out;

 error:
	kp_puts(ks, "error: table histogram only handle "
			" (key: string/number val: number)\n");
 out:
	kp_free(ks, thr);
}

#define HISTOGRAM_DEFAULT_TOP_NUM	20

#define DISTRIBUTION_STR "------------- Distribution -------------"
void kp_table_histogram(ktap_state *ks, ktap_table *t)
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

static void merge_table(ktap_state *ks, ktap_table *t1, ktap_table *t2)
{
	unsigned long __maybe_unused flags;
	ktap_value *newv;
	ktap_value n;
	int i;

	kp_table_lock(t1);
	kp_table_lock(t2);

	for (i = 0; i < t1->sizearray; i++) {
		ktap_value *v = &t1->array[i];
		ktap_stat_data *sd;

		if (is_nil(v))
			continue;

		set_number(&n, i);

		newv = table_set(ks, t2, &n);
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
		if (is_nil(newv)) {
			*read_sd(t2, newv) = *read_sd(t1, gval(node));
			set_statdata(newv, read_sd(t2, newv));
		} else
			statdata_add(read_sd(t1, gval(node)),
				     read_sd(t2, newv));
	}

	kp_table_unlock(t2);
	kp_table_unlock(t1);
}

ktap_table *kp_ptable_synthesis(ktap_state *ks, ktap_ptable *ph)
{
	ktap_table *agg;
	int cpu;

	agg = ph->agg;

	/* clear the table content before store new elements */
	kp_table_clear(ks, agg);

	for_each_possible_cpu(cpu) {
		ktap_table **t = per_cpu_ptr(ph->tbl, cpu);
		merge_table(ks, *t, agg);
	}

	return agg;
}

void kp_ptable_dump(ktap_state *ks, ktap_ptable *ph)
{
	kp_table_dump(ks, kp_ptable_synthesis(ks, ph));
}

ktap_ptable *kp_ptable_new(ktap_state *ks)
{
	ktap_ptable *ph;
	int cpu;

	ph = &kp_newobject(ks, KTAP_TPTABLE, sizeof(ktap_ptable),
			NULL)->ph;
	ph->tbl = alloc_percpu(ktap_table *);

	for_each_possible_cpu(cpu) {
		ktap_table **t = per_cpu_ptr(ph->tbl, cpu);
		*t = kp_table_new(ks);

		(*t)->with_stats = 1;

		/* todo: make this value to be configuable, MAXENTRIES? */
		table_resize(ks, *t, 0, 2000);
	}

	ph->agg = kp_table_new(ks);
	ph->agg->with_stats = 1;
	table_resize(ks, ph->agg, 0, 2000);

	return ph;
}

void kp_ptable_free(ktap_state *ks, ktap_ptable *ph)
{
	free_percpu(ph->tbl);
	kp_free(ks, ph);
}

void kp_ptable_set(ktap_state *ks, ktap_ptable *ph,
				   ktap_value *key, ktap_value *val)
{
	ktap_table *t = *__this_cpu_ptr(ph->tbl);
	unsigned long __maybe_unused flags;
	ktap_value *v;
	ktap_stat_data *sd;
	int aggval;;

	if (unlikely(!is_number(val))) {
		kp_error(ks, "add non number value to aggregation table\n");
		return;
	}

	aggval = nvalue(val);

	kp_table_lock(t);

	v = table_set(ks, t, key);
	sd = read_sd(t, v);

	if (is_nil(v)) {
		sd->count = 1;
		sd->sum = sd->min = sd->max = aggval;
		set_statdata(v, sd);
		kp_table_unlock(t);
		return;
	}

	sd->count++;
	sd->sum += aggval;
	if (aggval > sd->max)
		sd->max = aggval;
	if (aggval < sd->min)
		sd->min = aggval;

	kp_table_unlock(t);
}


void kp_ptable_get(ktap_state *ks, ktap_ptable *ph,
				   ktap_value *key, ktap_value *val)
{
	unsigned long __maybe_unused flags;
	ktap_stat_data sd, *aggsd;
	const ktap_value *v;
	ktap_value *aggval;
	int cpu;

	sd.count = sd.sum = sd.max = sd.min = -1;

	for_each_possible_cpu(cpu) {
		ktap_table **t = per_cpu_ptr(ph->tbl, cpu);

		kp_table_lock(*t);
		v = table_get(*t, key);
		if (is_nil(v)) {
			kp_table_unlock(*t);
			continue;
		}

		if (sd.count == -1) {
			sd = *read_sd(*t, v);
			kp_table_unlock(*t);
			continue;
		}

		statdata_add(read_sd(*t, v), &sd);
		kp_table_unlock(*t);
	}

	if (sd.count == -1) {
		set_nil(val);
		return;
	}

	kp_table_lock(ph->agg);
	aggval = table_set(ks, ph->agg, key);
	aggsd = read_sd(ph->agg, aggval);
	*aggsd = sd;
	set_statdata(aggval, aggsd);
	set_statdata(val, aggsd);
	kp_table_unlock(ph->agg);
}

void kp_ptable_histogram(ktap_state *ks, ktap_ptable *ph)
{
	kp_table_histogram(ks, kp_ptable_synthesis(ks, ph));
}
#endif
