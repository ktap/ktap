/*
 * kp_str.c - ktap string data struction manipulation
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

#include "../include/ktap_types.h"
#include "kp_obj.h"
#include "kp_str.h"

#ifdef __KERNEL__
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include "ktap.h"
#include "kp_transport.h"
#include "kp_vm.h"
#endif

#define STRING_MAXSHORTLEN	40

int kp_tstring_cmp(const ktap_string *ls, const ktap_string *rs)
{
	const char *l = getstr(ls);
	size_t ll = ls->tsv.len;
	const char *r = getstr(rs);
	size_t lr = rs->tsv.len;

	for (;;) {
		int temp = strcmp(l, r);
		if (temp != 0)
			return temp;
		else {
			/* strings are equal up to a `\0' */

			/* index of first `\0' in both strings */
			size_t len = strlen(l);

			/* r is finished? */
			if (len == lr)
				return (len == ll) ? 0 : 1;
			else if (len == ll)  /* l is finished? */
				return -1;

			/*
			 * both strings longer than `len';
			 * go on comparing (after the `\0')
			 */
			len++;
			l += len; ll -= len; r += len; lr -= len;
		}
	}
}

/*
 * equality for long strings
 */
int kp_tstring_eqlngstr(ktap_string *a, ktap_string *b)
{
	size_t len = a->tsv.len;

	return (a == b) || ((len == b->tsv.len) &&
		(memcmp(getstr(a), getstr(b), len) == 0));
}

/*
 * equality for strings
 */
int kp_tstring_eqstr(ktap_string *a, ktap_string *b)
{
	return (a->tsv.tt == b->tsv.tt) &&
	       (a->tsv.tt == KTAP_TSHRSTR ? eqshrstr(a, b) :
				kp_tstring_eqlngstr(a, b));
}

#define STRING_HASHLIMIT	5
unsigned int kp_string_hash(const char *str, size_t l, unsigned int seed)
{
	unsigned int h = seed ^ l;
	size_t l1;
	size_t step = (l >> STRING_HASHLIMIT) + 1;

	for (l1 = l; l1 >= step; l1 -= step)
		h = h ^ ((h<<5) + (h>>2) + (u8)(str[l1 - 1]));

	return h;
}


/*
 * resizes the string table
 */
void kp_tstring_resize(ktap_state *ks, int newsize)
{
	int i;
	ktap_stringtable *tb = &G(ks)->strt;

	if (newsize > tb->size) {
		kp_realloc(ks, tb->hash, tb->size, newsize, ktap_gcobject *);

	for (i = tb->size; i < newsize; i++)
		tb->hash[i] = NULL;
	}

	/* rehash */
	for (i = 0; i < tb->size; i++) {
		ktap_gcobject *p = tb->hash[i];
		tb->hash[i] = NULL;

		while (p) {
			ktap_gcobject *next = gch(p)->next;
			unsigned int h = lmod(gco2ts(p)->hash, newsize);

			gch(p)->next = tb->hash[h];
			tb->hash[h] = p;
			p = next;
		}
	}

	if (newsize < tb->size) {
		/* shrinking slice must be empty */
		kp_realloc(ks, tb->hash, tb->size, newsize, ktap_gcobject *);
	}

	tb->size = newsize;
}

/*
 * creates a new string object
 */
static ktap_string *createstrobj(ktap_state *ks, const char *str, size_t l,
				 int tag, unsigned int h, ktap_gcobject **list)
{
	ktap_string *ts;
	size_t totalsize;  /* total size of TString object */

	totalsize = sizeof(ktap_string) + ((l + 1) * sizeof(char));
	ts = &kp_newobject(ks, tag, totalsize, list)->ts;
	ts->tsv.len = l;
	ts->tsv.hash = h;
	ts->tsv.extra = 0;
	memcpy(ts + 1, str, l * sizeof(char));
	((char *)(ts + 1))[l] = '\0';  /* ending 0 */
	return ts;
}

/*
 * creates a new short string, inserting it into string table
 */
static ktap_string *newshrstr(ktap_state *ks, const char *str, size_t l,
			  unsigned int h)
{
	ktap_gcobject **list;
	ktap_stringtable *tb = &G(ks)->strt;
	ktap_string *s;

	if (tb->nuse >= (int)tb->size)
		kp_tstring_resize(ks, tb->size * 2);  /* too crowded */

	list = &tb->hash[lmod(h, tb->size)];
	s = createstrobj(ks, str, l, KTAP_TSHRSTR, h, list);
	tb->nuse++;
	return s;
}

/*
 * checks whether short string exists and reuses it or creates a new one
 */
static ktap_string *internshrstr(ktap_state *ks, const char *str, size_t l)
{
	ktap_gcobject *o;
	ktap_global_state *g = G(ks);
	ktap_string *ts;
	unsigned int h = kp_string_hash(str, l, g->seed);
	unsigned long __maybe_unused flags;

#ifdef __KERNEL__
	local_irq_save(flags);
	arch_spin_lock(&G(ks)->str_lock);
#endif

	for (o = g->strt.hash[lmod(h, g->strt.size)]; o != NULL;
	     o = gch(o)->next) {
		ts = rawgco2ts(o);

		if (h == ts->tsv.hash && ts->tsv.len == l &&
		   (memcmp(str, getstr(ts), l * sizeof(char)) == 0))
			goto out;
	}

	ts = newshrstr(ks, str, l, h);  /* not found; create a new string */

 out:
#ifdef __KERNEL__
	arch_spin_unlock(&G(ks)->str_lock);
	local_irq_restore(flags);
#endif
	return ts;
}


/*
 * new string (with explicit length)
 */
ktap_string *kp_tstring_newlstr(ktap_state *ks, const char *str, size_t l)
{
	/* short string? */
	if (l <= STRING_MAXSHORTLEN)
		return internshrstr(ks, str, l);
	else
		return createstrobj(ks, str, l, KTAP_TLNGSTR, G(ks)->seed,
				    NULL);
}

ktap_string *kp_tstring_newlstr_local(ktap_state *ks, const char *str, size_t l)
{
	return createstrobj(ks, str, l, KTAP_TLNGSTR, G(ks)->seed,
			    &ks->gclist);
}

/*
 * new zero-terminated string
 */
ktap_string *kp_tstring_new(ktap_state *ks, const char *str)
{
	return kp_tstring_newlstr(ks, str, strlen(str));
}

ktap_string *kp_tstring_new_local(ktap_state *ks, const char *str)
{
	return createstrobj(ks, str, strlen(str), KTAP_TLNGSTR, G(ks)->seed,
			    &ks->gclist);
}

void kp_tstring_freeall(ktap_state *ks)
{
	ktap_global_state *g = G(ks);
	int h;

	for (h = 0; h < g->strt.size; h++) {
		ktap_gcobject *o, *next;
		o = g->strt.hash[h];
		while (o) {
			next = gch(o)->next;
			kp_free(ks, o);
			o = next;
		}
		g->strt.hash[h] = NULL;
	}

	kp_free(ks, g->strt.hash);
}

/* todo: dump long string, strt table only contain short string */
void kp_tstring_dump(ktap_state *ks)
{
	ktap_gcobject *o;
	ktap_global_state *g = G(ks);
	int h;

	kp_printf(ks, "tstring dump: strt size: %d, nuse: %d\n", g->strt.size,
								 g->strt.nuse);
	for (h = 0; h < g->strt.size; h++) {
		for (o = g->strt.hash[h]; o != NULL; o = gch(o)->next) {
			ktap_string *ts = rawgco2ts(o);
			kp_printf(ks, "%s [%d]\n", getstr(ts), (int)ts->tsv.len);
		}
	}
}

#ifdef __KERNEL__
/* kp_str_fmt - printf implementation */

/* macro to `unsign' a character */
#define uchar(c)	((unsigned char)(c))

#define L_ESC		'%'

/* valid flags in a format specification */
#define FLAGS	"-+ #0"

#define INTFRMLEN	"ll"
#define INTFRM_T	long long

/*
 * maximum size of each format specification (such as '%-099.99d')
 * (+10 accounts for %99.99x plus margin of error)
 */
#define MAX_FORMAT	(sizeof(FLAGS) + sizeof(INTFRMLEN) + 10)

static const char *scanformat(ktap_state *ks, const char *strfrmt, char *form)
{
	const char *p = strfrmt;
	while (*p != '\0' && strchr(FLAGS, *p) != NULL)
		p++;  /* skip flags */

	if ((size_t)(p - strfrmt) >= sizeof(FLAGS)/sizeof(char)) {
		kp_error(ks, "invalid format (repeated flags)\n");
		return NULL;
	}

	if (isdigit(uchar(*p)))
		p++;  /* skip width */

	if (isdigit(uchar(*p)))
		p++;  /* (2 digits at most) */

	if (*p == '.') {
		p++;
		if (isdigit(uchar(*p)))
			p++;  /* skip precision */
		if (isdigit(uchar(*p)))
			p++;  /* (2 digits at most) */
	}

	if (isdigit(uchar(*p))) {
		kp_error(ks, "invalid format (width or precision too long)\n");
		return NULL;
	}

	*(form++) = '%';
	memcpy(form, strfrmt, (p - strfrmt + 1) * sizeof(char));
	form += p - strfrmt + 1;
	*form = '\0';
	return p;
}


/*
 * add length modifier into formats
 */
static void addlenmod(char *form, const char *lenmod)
{
	size_t l = strlen(form);
	size_t lm = strlen(lenmod);
	char spec = form[l - 1];

	strcpy(form + l - 1, lenmod);
	form[l + lm - 1] = spec;
	form[l + lm] = '\0';
}


static void ktap_argerror(ktap_state *ks, int narg, const char *extramsg)
{
	kp_error(ks, "bad argument #%d: (%s)\n", narg, extramsg);
}

int kp_str_fmt(ktap_state *ks, struct trace_seq *seq)
{
	int arg = 1;
	size_t sfl;
	ktap_value *arg_fmt = kp_arg(ks, 1);
	int argnum = kp_arg_nr(ks);
	const char *strfrmt, *strfrmt_end;

	strfrmt = svalue(arg_fmt);
	sfl = rawtsvalue(arg_fmt)->tsv.len;
	strfrmt_end = strfrmt + sfl;

	while (strfrmt < strfrmt_end) {
		if (*strfrmt != L_ESC)
			trace_seq_putc(seq, *strfrmt++);
		else if (*++strfrmt == L_ESC)
			trace_seq_putc(seq, *strfrmt++);
		else { /* format item */
			char form[MAX_FORMAT];

			if (++arg > argnum) {
				ktap_argerror(ks, arg, "no value");
				return -1;
			}

			strfrmt = scanformat(ks, strfrmt, form);
			switch (*strfrmt++) {
			case 'c':
				trace_seq_printf(seq, form,
						 nvalue(kp_arg(ks, arg)));
				break;
			case 'd':  case 'i': {
				ktap_number n = nvalue(kp_arg(ks, arg));
				INTFRM_T ni = (INTFRM_T)n;
				addlenmod(form, INTFRMLEN);
				trace_seq_printf(seq, form, ni);
				break;
			}
			case 'p': {
				char str[KSYM_SYMBOL_LEN];
				SPRINT_SYMBOL(str, nvalue(kp_arg(ks, arg)));
				_trace_seq_puts(seq, str);
				break;
			}
			case 'o':  case 'u':  case 'x':  case 'X': {
				ktap_number n = nvalue(kp_arg(ks, arg));
				unsigned INTFRM_T ni = (unsigned INTFRM_T)n;
				addlenmod(form, INTFRMLEN);
				trace_seq_printf(seq, form, ni);
				break;
			}
			case 's': {
				ktap_value *v = kp_arg(ks, arg);
				const char *s;
				size_t l;

				if (is_nil(v)) {
					_trace_seq_puts(seq, "nil");
					return 0;
				}

				if (is_event(v)) {
					kp_event_tostring(ks, seq);
					return 0;
				}

				s = svalue(v);
				l = rawtsvalue(v)->tsv.len;
				if (!strchr(form, '.') && l >= 100) {
					/*
					 * no precision and string is too long
					 * to be formatted;
					 * keep original string
					 */
					_trace_seq_puts(seq, s);
					break;
				} else {
					trace_seq_printf(seq, form, s);
					break;
				}
			}
			default: /* also treat cases `pnLlh' */
				kp_error(ks, "invalid option " KTAP_QL("%%%c")
					     " to " KTAP_QL("format"),
					     *(strfrmt - 1));
			}
		}
	}

	return 0;
}
#endif

