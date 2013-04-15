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
	case KTAP_TLNGSTR:
		ktap_printf(ks, "SHRSTR #%s", svalue(v));
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
	case KTAP_TLNGSTR:
		ktap_printf(ks, "\"%s\"", getstr(rawtsvalue(v)));
		break;
	case KTAP_TUSERDATA:
		ktap_printf(ks, "%d", uvalue(v));
		break;
	case KTAP_TTABLE:
		table_dump(ks, hvalue(v));
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

static Udata *ktap_newudata(ktap_State *ks, size_t s)
{
	Udata *u;

	u = &newobject(ks, KTAP_TUSERDATA, sizeof(Udata) + s, NULL)->u;
	u->uv.len = s;
	return u;
}

void *ktap_newuserdata(ktap_State *ks, size_t size)
{
	Udata *u;

	u = ktap_newudata(ks, size);
	return u + 1;
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
 * Generic Buffer manipulation
 */

/*
 * ** check whether buffer is using a userdata on the stack as a temporary
 * ** buffer
 * */
#define buffonstack(B)  ((B)->b != (B)->initb)


/*
 * returns a pointer to a free area with at least 'sz' bytes
 */
char *ktap_prepbuffsize(ktap_Buffer *B, size_t sz)
{
	ktap_State *ks = B->ks;

	if (B->size - B->n < sz) {  /* not enough space? */
		char *newbuff;
		size_t newsize = B->size * 2;  /* double buffer size */

		if (newsize - B->n < sz)  /* not bit enough? */
			newsize = B->n + sz;

		if (newsize < B->n || newsize - B->n < sz)
			ktap_runerror(ks, "buffer too large");

		/* create larger buffer */
		//newbuff = (char *)ktap_newuserdata(ks, newsize * sizeof(char));
		newbuff = (char *)ktap_malloc(ks, newsize * sizeof(char));
		/* move content to new buffer */
		memcpy(newbuff, B->b, B->n * sizeof(char));
		/* todo: remove old buffer now, cannot use ktap_free directly */
		#if 0
		if (buffonstack(B))
			ktap_remove(ks, -2);  /* remove old buffer */
		#endif
		B->b = newbuff;
		B->size = newsize;
	}
	return &B->b[B->n];
}


void ktap_addlstring(ktap_Buffer *B, const char *s, size_t l)
{
	char *b = ktap_prepbuffsize(B, l);
	memcpy(b, s, l * sizeof(char));
	ktap_addsize(B, l);
}


void ktap_addstring(ktap_Buffer *B, const char *s)
{
	ktap_addlstring(B, s, strlen(s));
}


void ktap_pushresult(ktap_Buffer *B)
{
	ktap_State *ks = B->ks;

	setsvalue(ks->top, tstring_newlstr(ks, B->b, B->n));
        incr_top(ks);

	/* todo: remove old buffer now, cannot use ktap_free directly */
	#if 0
	if (buffonstack(B))
		ktap_remove(ks, -2);  /* remove old buffer */
	#endif
}


void ktap_buffinit(ktap_State *ks, ktap_Buffer *B)
{
	B->ks = ks;
	B->b = B->initb;
	B->n = 0;
	B->size = KTAP_BUFFERSIZE;
}

void ktap_bufffree(ktap_State *ks, ktap_Buffer *B)
{
	if (B->b != B->initb)
		ktap_free(ks, B->b);
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


