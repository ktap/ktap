/*
 * table.c - ktap table data structure manipulation function
 *
 * Copyright 2013 The ktap Project Developers.
 * See the COPYRIGHT file at the top-level directory of this distribution.
 *
 * The part of code is copied from lua initially in this file.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
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
#include <linux/version.h>
#else
#include "../include/ktap_types.h"

static inline void sort(void *base, size_t num, size_t size,
			int (*cmp_func)(const void *, const void *),
			void (*swap_func)(void *, void *, int size))
{}
#endif


#ifdef __KERNEL__
#define kp_table_lock_init(t)	\
	do {	\
		t->lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;	\
	} while (0)
#define kp_table_lock(t)	\
	do {	\
		local_irq_save(flags);	\
		arch_spin_lock(&t->lock);	\
	} while (0)
#define kp_table_unlock(t)	\
	do {	\
		arch_spin_unlock(&t->lock);	\
		local_irq_restore(flags);	\
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
	{{NILCONSTANT, NULL}}, /* key */
};

#define gnode(t,i)      (&(t)->node[i])
#define gkey(n)         (&(n)->i_key.tvk)
#define gval(n)         (&(n)->i_val)
#define gnext(n)        ((n)->i_key.nk.next)

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


ktap_table *kp_table_new(ktap_state *ks)
{
	ktap_table *t = &kp_newobject(ks, KTAP_TTABLE, sizeof(ktap_table),
				      NULL)->h;
	t->flags = (u8)(~0);
	t->array = NULL;
	t->sizearray = 0;
	t->node = (ktap_tnode *)dummynode;
	setnodevector(ks, t, 0);

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
		if (ttisnumber(gkey(n)) && nvalue(gkey(n)) == key)
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
	case KTAP_TLCF:
		return hashpointer(t, fvalue(key));
	case KTAP_TBTRACE:
		/* use first entry as hash key, cannot use gcvalue as key */
		return hashpointer(t, btvalue(key)->entries[0]);
	default:
		return hashpointer(t, gcvalue(key));
	}
}

static int arrayindex(const ktap_value *key)
{
	if (ttisnumber(key)) {
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

	if (ttisnil(key))
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
	        if (!ttisnil(&t->array[i])) {  /* a non-nil value? */
			setnvalue(key, i+1);
			setobj(key+1, &t->array[i]);
			kp_table_unlock(t);
			return 1;
		}
	}

	for (i -= t->sizearray; i < sizenode(t); i++) {  /* then hash part */
		if (!ttisnil(gval(gnode(t, i)))) {  /* a non-nil value? */
			setobj(key, gkey(gnode(t, i)));
			setobj(key+1, gval(gnode(t, i)));
			kp_table_unlock(t);
			return 1;
		}
	}

	kp_table_unlock(t);
	return 0;  /* no more elements */
}



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
			if (!ttisnil(&t->array[i-1]))
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
		if (!isnil(gval(n))) {
			ause += countint(gkey(n), nums);
			totaluse++;
		}
	}

	*pnasize += ause;
	return totaluse;
}


static void setarrayvector(ktap_state *ks, ktap_table *t, int size)
{
	int i;

	kp_realloc(ks, t->array, t->sizearray, size, ktap_value);
	for (i = t->sizearray; i < size; i++)
		setnilvalue(&t->array[i]);

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
		for (i = 0; i < size; i++) {
			ktap_tnode *n = gnode(t, i);
			gnext(n) = NULL;
			setnilvalue(gkey(n));
			setnilvalue(gval(n));
		}
	}

	t->lsizenode = (u8)lsize;
	t->lastfree = gnode(t, size);  /* all positions are free */
}

static void table_resize(ktap_state *ks, ktap_table *t, int nasize, int nhsize)
{
	int i;
	int oldasize = t->sizearray;
	int oldhsize = t->lsizenode;
	ktap_tnode *nold = t->node;  /* save old hash ... */

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
		for (i=nasize; i<oldasize; i++) {
			if (!ttisnil(&t->array[i]))
				table_setint(ks, t, i + 1, &t->array[i]);
		}

		/* shrink array */
		kp_realloc(ks, t->array, oldasize, nasize, ktap_value);
	}

	/* re-insert elements from hash part */
	for (i = twoto(oldhsize) - 1; i >= 0; i--) {
		ktap_tnode *old = nold+i;
		if (!ttisnil(gval(old))) {
			/*
			 * doesn't need barrier/invalidate cache, as entry was
			 * already present in the table
			 */
			setobj(table_set(ks, t, gkey(old)), gval(old));
		}
	}

	if (!isdummy(nold))
		kp_free(ks, nold); /* free old array */
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
	if (isnil(gkey(t->lastfree)))
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
	if (!isnil(gval(mp)) || isdummy(mp)) {  /* main position is taken? */
		ktap_tnode *othern;
		ktap_tnode *n = getfreepos(t);  /* get a free place */
		if (n == NULL) {  /* cannot find a free place? */
			rehash(ks, t, key);  /* grow table */
			/* whatever called 'newkey' take care of TM cache and GC barrier */
			return table_set(ks, t, key);  /* insert key into grown table */
		}

		othern = mainposition(t, gkey(mp));
		if (othern != mp) {  /* is colliding node out of its main position? */
			/* yes; move colliding node into free position */
			while (gnext(othern) != mp)
				othern = gnext(othern);  /* find previous */
			gnext(othern) = n;  /* redo the chain with `n' in place of `mp' */
			*n = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
			gnext(mp) = NULL;  /* now `mp' is free */
			setnilvalue(gval(mp));
		} else {  /* colliding node is in its own main position */
			/* new node will go into free position */
			gnext(n) = gnext(mp);  /* chain new position */
			gnext(mp) = n;
			mp = n;
		}
	}

	/* special handling for cloneable object, maily for btrace object */
	if (ttisclone(key))
		kp_objclone(ks, key, &newkey);
	else
		newkey = *key;

	setobj(gkey(mp), &newkey);
	return gval(mp);
}


/*
 * search function for short strings
 */
static const ktap_value *table_getstr(ktap_table *t, ktap_string *key)
{
	ktap_tnode *n = hashstr(t, key);

	do {  /* check whether `key' is somewhere in the chain */
		if (ttisshrstring(gkey(n)) && eqshrstr(rawtsvalue(gkey(n)),
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

	if (isnil(key)) {
		kp_printf(ks, "table index is nil\n");
		kp_exit(ks);
		return;
	}

	kp_table_lock(t);
	setobj(table_set(ks, t, key), val);
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
		setnvalue(&k, key);
		cell = table_newkey(ks, t, &k);
	}

	setobj(cell, v);
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

	if (isnil(key)) {
		kp_printf(ks, "table index is nil\n");
		kp_exit(ks);
		return;
	}

	kp_table_lock(t);

	v = table_set(ks, t, key);
	if (isnil(v)) {
		setnvalue(v, n);
	} else
		setnvalue(v, nvalue(v) + n);

	kp_table_unlock(t);
}

int kp_table_length(ktap_state *ks, ktap_table *t)
{
	unsigned long __maybe_unused flags;
	int i, len = 0;

	kp_table_lock(t);

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (isnil(v))
			continue;
		len++;
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *n = &t->node[i];

		if (isnil(gkey(n)))
			continue;

		len++;
	}
	
	kp_table_unlock(t);
	return len;
}

void kp_table_free(ktap_state *ks, ktap_table *t)
{
	if (t->sizearray > 0)
		kp_free(ks, t->array);
	if (!isdummy(t->node))
		kp_free(ks, t->node);

	kp_free(ks, t);
}

void kp_table_dump(ktap_state *ks, ktap_table *t)
{
	int i, count = 0;

	kp_puts(ks, "{");
	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (isnil(v))
			continue;

		if (count)
			kp_puts(ks, ", ");

		kp_printf(ks, "(%d: ", i + 1);
		kp_showobj(ks, v);
		kp_puts(ks, ")");
		count++;
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *n = &t->node[i];

		if (isnil(gkey(n)))
			continue;

		if (count)
			kp_puts(ks, ", ");

		kp_puts(ks, "(");
		kp_showobj(ks, gkey(n));
		kp_puts(ks, ": ");
		kp_showobj(ks, gval(n));
		kp_puts(ks, ")");
		count++;
	}
	kp_puts(ks, "}");
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

#define HISTOGRAM_DEFAULT_TOP_NUM	20

#define DISTRIBUTION_STR "------------- Distribution -------------"
/* histogram: key should be number or string, value must be number */
void kp_table_histogram(ktap_state *ks, ktap_table *t)
{
	struct table_hist_record *thr;
	unsigned long __maybe_unused flags;
	char dist_str[40];
	int i, ratio, total = 0, count = 0, top_num;
	int size;

	size = sizeof(*thr) * (t->sizearray + sizenode(t));
	thr = kp_malloc(ks, size);
	if (!thr) {
		kp_error(ks, "Cannot allocate %d of histogram memory", size);
		return;
	}

	kp_table_lock(t);

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (isnil(v))
			continue;

		if (!ttisnumber(v))
			goto error;

		setnvalue(&thr[count++].key, i + 1);
		total += nvalue(v);
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *n = &t->node[i];
		int num;

		if (isnil(gkey(n)))
			continue;

		if (!ttisnumber(gval(n)))
			goto error;

		num = nvalue(gval(n));
		setobj(&thr[count].key, gkey(n));
		setobj(&thr[count].val, gval(n));
		count++;
		total += nvalue(gval(n));
	}

	kp_table_unlock(t);

	sort(thr, count, sizeof(struct table_hist_record),
	     hist_record_cmp, NULL);

	kp_printf(ks, "%32s%s%s\n", "value ", DISTRIBUTION_STR, " count");
	dist_str[sizeof(dist_str) - 1] = '\0';

	top_num = min(HISTOGRAM_DEFAULT_TOP_NUM, count);
	for (i = 0; i < top_num; i++) {
		ktap_value *key = &thr[i].key;
		ktap_value *val = &thr[i].val;

		memset(dist_str, ' ', sizeof(dist_str) - 1);
		ratio = (nvalue(val) * (sizeof(dist_str) - 1)) / total;
		memset(dist_str, '@', ratio);

		if (ttisstring(key)) {
			char buf[32 + 1] = {0};

			string_convert(buf, svalue(key));
			kp_printf(ks, "%32s |%s%-7d\n", buf, dist_str,
				      nvalue(val));
		} else {
			char str[KSYM_SYMBOL_LEN];
			char buf[32 + 1] = {0};

			/* suppose it's a symbol, fix it in future */
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 5, 0)
			sprint_symbol_no_offset(str, nvalue(key));
#else
			sprint_symbol(str, nvalue(key));
#endif
			string_convert(buf, str);
			kp_printf(ks, "%32s | %s%-7d\n", buf, dist_str,
				      nvalue(val));
		}
	}

	if (count > HISTOGRAM_DEFAULT_TOP_NUM)
		kp_printf(ks, "%32s |\n", "...");

	goto out;

 error:
	kp_puts(ks, "error: table histogram only handle "
			" (key: string/number val: number)\n");
 out:
	kp_free(ks, thr);
}
#endif
