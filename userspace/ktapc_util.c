/*
 * ktapc_util.c
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/ktap_types.h"
#include "../include/ktap_opcodes.h"
#include "ktapc.h"

void *ktapc_reallocv(void *block, size_t osize, size_t nsize)
{
	return realloc(block, nsize);
}

static
ktap_gcobject *newobject(int type, size_t size)
{
	ktap_gcobject *o;

	o = malloc(size);
	gch(o)->tt = type;
	return o;
}

ktap_closure *ktapc_newclosure(int n)
{
	ktap_closure *cl;

	cl = (ktap_closure *)newobject(KTAP_TYPE_CLOSURE, sizeof(*cl));
	cl->p = NULL;
	cl->nupvalues = n;
	while (n--)
		cl->upvals[n] = NULL;

	return cl;
}

ktap_proto *ktapc_newproto()
{
	ktap_proto *f;
	f = (ktap_proto *)newobject(KTAP_TYPE_PROTO, sizeof(*f));
	f->k = NULL;
 	f->sizek = 0;
	f->p = NULL;
	f->sizep = 0;
	f->code = NULL;
	f->cache = NULL;
	f->sizecode = 0;
	f->lineinfo = NULL;
	f->sizelineinfo = 0;
	f->upvalues = NULL;
	f->sizeupvalues = 0;
	f->numparams = 0;
	f->is_vararg = 0;
	f->maxstacksize = 0;
	f->locvars = NULL;
	f->sizelocvars = 0;
	f->linedefined = 0;
	f->lastlinedefined = 0;
	f->source = NULL;
	return f;
}

#define NILCONSTANT     {NULL}, KTAP_TYPE_NIL
static const struct ktap_value ktap_nilobjectv = {NILCONSTANT};
#define ktap_nilobject  (&ktap_nilobjectv)

#define gnode(t,i)	(&(t)->node[i])
#define gkey(n)		(&(n)->i_key.tvk)
#define gval(n)		(&(n)->i_val)

const ktap_value *ktapc_tab_get(ktap_tab *t, const ktap_value *key)
{
	int table_node_nr = t->lastfree - t->node;
	int i;

	switch (ttype(key)) {
	case KTAP_TYPE_NIL:
		return ktap_nilobject;
	case KTAP_TYPE_NUMBER:
		for (i = 0; i < table_node_nr; i++) {
			ktap_value *v = gkey(gnode(t, i));
			if (is_number(v) && nvalue(key) == nvalue(v))
				return gval(gnode(t, i));
		}
		break;
	case KTAP_TYPE_SHRSTR:
		for (i = 0; i < table_node_nr; i++) {
			ktap_value *v = gkey(gnode(t, i));
			if (is_string(v) &&
				ktapc_ts_eqstr(rawtsvalue(key), rawtsvalue(v)))
				return gval(gnode(t, i));
		}
		break;
	default:
		for (i = 0; i < table_node_nr; i++) {
			if (ktapc_equalobj(key, gkey(gnode(t, i))))
				return gval(gnode(t, i));
		}
		break;
	}

	return ktap_nilobject;
}

void ktapc_tab_setvalue(ktap_tab *t, const ktap_value *key, ktap_value *val)
{
	const ktap_value *v = ktapc_tab_get(t, key);

	if (v != ktap_nilobject) {
		set_obj((ktap_value *)v, val);
	} else {
		if (t->lastfree == t->node + t->sizearray) {
			int size = t->sizearray * sizeof(ktap_tnode);
			t->node = realloc(t->node, size * 2);
			memset(t->node + t->sizearray, 0, size);
			t->lastfree = t->node + t->sizearray;
			t->sizearray *= 2;
		}

		ktap_tnode *n = t->lastfree;
		set_obj(gkey(n), key);
		set_obj(gval(n), val);
		t->lastfree++;
	}
}

ktap_tab *ktapc_tab_new(void)
{
	ktap_tab *t = &newobject(KTAP_TYPE_TABLE, sizeof(ktap_tab))->h;
	t->sizearray = 100;
	t->node = (ktap_tnode *)malloc(t->sizearray * sizeof(ktap_tnode));
	memset(t->node, 0, t->sizearray * sizeof(ktap_tnode));
	t->lastfree = &t->node[0];
	return t;
}

static void ktapc_tab_dump(ktap_tab *t)
{
	int table_node_nr = t->lastfree - t->node;
	int i;

	for (i = 0; i < table_node_nr; i++) {
		if (!is_nil(gkey(gnode(t, i)))) {
			ktapc_showobj(gkey(gnode(t, i)));	
			printf(":\t");
			ktapc_showobj(gval(gnode(t, i)));
		}	
	}
}

/* simple string array */
static ktap_string **ktapc_strtab;
static int ktapc_strtab_size = 1000; /* initial size */
static int ktapc_strtab_nr;

void ktapc_init_stringtable(void)
{
	int size = ktapc_strtab_size * sizeof(ktap_string *);

	ktapc_strtab = malloc(size);
	if (!ktapc_strtab) {
		fprintf(stderr, "cannot allocate ktapc_stringtable\n");
		exit(-1);
	}

	memset(ktapc_strtab, 0, size);
	ktapc_strtab_nr = 0;
}

static ktap_string *stringtable_search(const char *str)
{
	int i;

	for (i = 0; i < ktapc_strtab_nr; i++)
		if (!strcmp(str, getstr(ktapc_strtab[i])))
			return ktapc_strtab[i];

	return NULL;
}

static void stringtable_insert(ktap_string *ts)
{
	ktapc_strtab[ktapc_strtab_nr++] = ts;

	if (ktapc_strtab_nr == ktapc_strtab_size) {
		int size = ktapc_strtab_size * sizeof(ktap_string *);
		ktapc_strtab = realloc(ktapc_strtab, size * 2);
		memset(ktapc_strtab + ktapc_strtab_size, 0, size);
		ktapc_strtab_size *= 2;
	}
}

static ktap_string *createstrobj(const char *str, size_t l)
{
	ktap_string *ts;
	size_t totalsize;  /* total size of TString object */

	totalsize = sizeof(ktap_string) + ((l + 1) * sizeof(char));
	ts = &newobject(KTAP_TYPE_SHRSTR, totalsize)->ts;
	ts->len = l;
	ts->extra = 0;
	memcpy(ts + 1, str, l * sizeof(char));
	((char *)(ts + 1))[l] = '\0';  /* ending 0 */
	return ts;
}

ktap_string *ktapc_ts_newlstr(const char *str, size_t l)
{
	ktap_string *ts = stringtable_search(str);

	if (ts)
		return ts;

	ts = createstrobj(str, l);
	stringtable_insert(ts);
	return ts;
}

ktap_string *ktapc_ts_new(const char *str)
{
	return ktapc_ts_newlstr(str, strlen(str));
}

int ktapc_ts_eqstr(ktap_string *a, ktap_string *b)
{
	return (a->tt == b->tt) &&
		((a == b) || ((a->len == b->len) &&
		!memcmp(getstr(a), getstr(b), a->len)));
}

static void ktapc_runerror(const char *err_msg_fmt, ...)
{
	va_list ap;

	fprintf(stderr, "ktapc_runerror\n");

	va_start(ap, err_msg_fmt);
	vfprintf(stderr, err_msg_fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

/*
 * todo: memory leak here
 */
char *ktapc_sprintf(const char *fmt, ...)
{
	char *msg = malloc(128);

	va_list argp;
	va_start(argp, fmt);
	vsprintf(msg, fmt, argp);
	va_end(argp);
	return msg;
}


#define MINSIZEARRAY	4

void *ktapc_growaux(void *block, int *size, size_t size_elems, int limit,
		    const char *what)
{
	void *newblock;
	int newsize;

	if (*size >= limit/2) {  /* cannot double it? */
		if (*size >= limit)  /* cannot grow even a little? */
			ktapc_runerror("too many %s (limit is %d)\n",
					what, limit);
		newsize = limit;  /* still have at least one free place */
	} else {
		newsize = (*size) * 2;
		if (newsize < MINSIZEARRAY)
			newsize = MINSIZEARRAY;  /* minimum size */
	}

	newblock = ktapc_reallocv(block, (*size) * size_elems, newsize * size_elems);
	*size = newsize;  /* update only when everything else is OK */
	return newblock;
}

int ktapc_equalobj(const ktap_value *t1, const ktap_value *t2)
{
	switch (ttype(t1)) {
	case KTAP_TYPE_NIL:
		return 1;
	case KTAP_TYPE_NUMBER:
		return nvalue(t1) == nvalue(t2);
	case KTAP_TYPE_BOOLEAN:
		return bvalue(t1) == bvalue(t2);  /* true must be 1 !! */
	case KTAP_TYPE_LIGHTUSERDATA:
		return pvalue(t1) == pvalue(t2);
	case KTAP_TYPE_CFUNCTION:
		return fvalue(t1) == fvalue(t2);
	case KTAP_TYPE_SHRSTR:
		return eqshrstr(rawtsvalue(t1), rawtsvalue(t2));
	default:
		return gcvalue(t1) == gcvalue(t2);
	}

	return 0;
}

void ktapc_showobj(const ktap_value *v)
{
	switch (ttype(v)) {
	case KTAP_TYPE_NIL:
		printf("nil");
		break;
	case KTAP_TYPE_NUMBER:
		printf("%ld", nvalue(v));
		break;
	case KTAP_TYPE_BOOLEAN:
		printf((bvalue(v) == 1) ? "true" : "false");
		break;
	case KTAP_TYPE_LIGHTUSERDATA:
		printf("0x%lx", (unsigned long)pvalue(v));
		break;
	case KTAP_TYPE_CFUNCTION:
		printf("0x%lx", (unsigned long)fvalue(v));
		break;
	case KTAP_TYPE_SHRSTR:
	case KTAP_TYPE_LNGSTR:
		printf("%s", svalue(v));
		break;
	case KTAP_TYPE_TABLE:
		ktapc_tab_dump(hvalue(v));
		break;
        default:
		printf("print unknown value type: %d\n", ttype(v));
		break;
	}
}

