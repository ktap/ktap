/*
 * object.c - ktap object generic operation
 *
 * Copyright (C) 2012-2013 Jovi Zhang
 *
 * Author: Jovi Zhang <bookjovi@gmail.com>
 *         zhangwei(Jovi) <jovi.zhangwei@huawei.com>
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
#include <linux/module.h>
#include <linux/slab.h>
#include "ktap.h"
#else
#include "ktap_types.h"
#endif


void obj_dump(ktap_State *ks, const Tvalue *v)
{
	switch (ttype(v)) {
	case KTAP_TNIL:
		ktap_printf(ks, "NIL");
		break;
	case KTAP_TNUMBER:
		ktap_printf(ks, "NUMBER %d", nvalue(v));
		break;
	case KTAP_TBOOLEAN:
		ktap_printf(ks, "BOOLEAN %d", bvalue(v));
		break;
	case KTAP_TLIGHTUSERDATA:
		ktap_printf(ks, "LIGHTUSERDATA %d", pvalue(v));
		break;
	case KTAP_TLCF:
		ktap_printf(ks, "LIGHTCFCUNTION 0x%x", fvalue(v));
		break;
	case KTAP_TSHRSTR:
		ktap_printf(ks, "SHRSTR %s", getstr(rawtsvalue(v)));
		break;
	case KTAP_TLNGSTR:
		ktap_printf(ks, "LNGSTR %s", getstr(rawtsvalue(v)));
		break;
	case KTAP_TUSERDATA:
		ktap_printf(ks, "USERDATA %d", uvalue(v));
		break;
	case KTAP_TTABLE:
		ktap_printf(ks, "TABLE 0x%x", hvalue(v));
		break;
        default:
		ktap_printf(ks, "GCVALUE 0x%x", gcvalue(v));
		break;
	}
}

void showobj(ktap_State *ks, const Tvalue *v)
{
	switch (ttype(v)) {
	case KTAP_TNIL:
		ktap_printf(ks, "nil");
		break;
	case KTAP_TNUMBER:
		ktap_printf(ks, "%d", nvalue(v));
		break;
	case KTAP_TBOOLEAN:
		ktap_printf(ks, "%s", (bvalue(v) == 1) ? "true" : "false");
		break;
	case KTAP_TLIGHTUSERDATA:
		ktap_printf(ks, "%d", pvalue(v));
		break;
	case KTAP_TLCF:
		ktap_printf(ks, "0x%x", fvalue(v));
		break;
	case KTAP_TSHRSTR:
		ktap_printf(ks, "%s", getstr(rawtsvalue(v)));
		break;
	case KTAP_TLNGSTR:
		ktap_printf(ks, "%s", getstr(rawtsvalue(v)));
		break;
	case KTAP_TUSERDATA:
		ktap_printf(ks, "%d", uvalue(v));
		break;
	case KTAP_TTABLE:
		/* todo: show table */
		ktap_printf(ks, "0x%x", hvalue(v));
		break;
        default:
		ktap_printf(ks, "[unknown value type: %d]", ttype(v));
		break;
	}
}


/*
 * equality of ktap values. ks == NULL means raw equality
 */
int equalobjv(ktap_State *ks, const Tvalue *t1, const Tvalue *t2)
{
	switch (ttype(t1)) {
	case KTAP_TNIL:
		return 1;
	case KTAP_TNUMBER:
		return nvalue(t1) == nvalue(t2);
	case KTAP_TBOOLEAN:
		return bvalue(t1) == bvalue(t2);  /* true must be 1 !! */
	case KTAP_TLIGHTUSERDATA:
		return pvalue(t1) == pvalue(t2);
	case KTAP_TLCF:
		return fvalue(t1) == fvalue(t2);
	case KTAP_TSHRSTR:
		return eqshrstr(rawtsvalue(t1), rawtsvalue(t2));
	case KTAP_TLNGSTR:
		return tstring_eqlngstr(rawtsvalue(t1), rawtsvalue(t2));
	case KTAP_TUSERDATA:
		if (uvalue(t1) == uvalue(t2))
			return 1;
		else if (ks == NULL)
			return 0;
	case KTAP_TTABLE:
		if (hvalue(t1) == hvalue(t2))
			return 1;
		else if (ks == NULL)
			return 0;
	default:
		return gcvalue(t1) == gcvalue(t2);
	}

	return 0;
}


void objlen(ktap_State *ks, StkId ra, const Tvalue *rb)
{
	switch(rb->type) {
	case KTAP_TTABLE:
		break;
	case KTAP_TSTRING:
		break;
	}
}


/* need to protect allgc field? */
Gcobject *newobject(ktap_State *ks, int type, size_t size, Gcobject **list)
{
	Gcobject *o;

	o = ktap_malloc(ks, sizeof(Gcobject) + size);
	if (list == NULL)
		list = &G(ks)->allgc;

	gch(o)->tt = type;
	gch(o)->marked = 0;
	gch(o)->next = *list;
	*list = o;

	return o;
}

Upval *ktap_newupval(ktap_State *ks)
{
	Upval *uv;

	uv = &newobject(ks, KTAP_TUPVAL, sizeof(Upval), NULL)->uv;
	uv->v = &uv->u.value;
	setnilvalue(uv->v);
	return uv;
}


Closure *ktap_newlclosure(ktap_State *ks, int n)
{
	Closure *cl;

	cl = (Closure *)newobject(ks, KTAP_TLCL, sizeof(*cl), NULL);
	cl->l.p = NULL;
	cl->l.nupvalues = n;
	while (n--)
		cl->l.upvals[n] = NULL;

	return cl;
}

static void free_proto(ktap_State *ks, Proto *f)
{
	ktap_free(ks, f->code);
	ktap_free(ks, f->p);
	ktap_free(ks, f->k);
	ktap_free(ks, f->lineinfo);
	ktap_free(ks, f->locvars);
	ktap_free(ks, f->upvalues);
	ktap_free(ks, f);
}

Proto *ktap_newproto(ktap_State *ks)
{
	Proto *f;
	f = (Proto *)newobject(ks, KTAP_TPROTO, sizeof(*f), NULL);
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

void free_all_gcobject(ktap_State *ks)
{
	Gcobject *o = G(ks)->allgc;
	Gcobject *next;

	while (o) {
		next = gch(o)->next;
		switch (gch(o)->tt) {
		case KTAP_TTABLE:
			table_free(ks, (Table *)o);
			break;
		case KTAP_TPROTO:
			free_proto(ks, (Proto *)o);
			break;
		default:
			ktap_free(ks, o);
		}
		o = next;
	}

	G(ks)->allgc = NULL;
}

/******************************************************************************/


/*
 * make header for precompiled chunks
 * if you change the code below be sure to update load_header and FORMAT above
 * and KTAPC_HEADERSIZE in ktap_types.h
 */
void ktap_header(u8 *h)
{
	int x = 1;

	memcpy(h, KTAP_SIGNATURE, sizeof(KTAP_SIGNATURE) - sizeof(char));
	h += sizeof(KTAP_SIGNATURE) - sizeof(char);
	*h++ = (u8)VERSION;
	*h++ = (u8)FORMAT;
	*h++ = (u8)(*(char*)&x);                    /* endianness */
	*h++ = (u8)(sizeof(int));
	*h++ = (u8)(sizeof(size_t));
	*h++ = (u8)(sizeof(Instruction));
	*h++ = (u8)(sizeof(ktap_Number));
	*h++ = (u8)(((ktap_Number)0.5) == 0);          /* is ktap_Number integral? */
	memcpy(h, KTAPC_TAIL, sizeof(KTAPC_TAIL) - sizeof(char));
}


