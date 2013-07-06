/*
 * object.c - ktap object generic operation
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
#else
#include "../include/ktap_types.h"
#endif


void kp_obj_dump(ktap_state *ks, const ktap_value *v)
{
	switch (ttype(v)) {
	case KTAP_TNIL:
		kp_printf(ks, "NIL");
		break;
	case KTAP_TNUMBER:
		kp_printf(ks, "NUMBER %d", nvalue(v));
		break;
	case KTAP_TBOOLEAN:
		kp_printf(ks, "BOOLEAN %d", bvalue(v));
		break;
	case KTAP_TLIGHTUSERDATA:
		kp_printf(ks, "LIGHTUSERDATA %d", pvalue(v));
		break;
	case KTAP_TLCF:
		kp_printf(ks, "LIGHTCFCUNTION 0x%x", fvalue(v));
		break;
	case KTAP_TSHRSTR:
	case KTAP_TLNGSTR:
		kp_printf(ks, "SHRSTR #%s", svalue(v));
		break;
	case KTAP_TUSERDATA:
		kp_printf(ks, "USERDATA %d", uvalue(v));
		break;
	case KTAP_TTABLE:
		kp_printf(ks, "TABLE 0x%x", hvalue(v));
		break;
        default:
		kp_printf(ks, "GCVALUE 0x%x", gcvalue(v));
		break;
	}
}

void kp_showobj(ktap_state *ks, const ktap_value *v)
{
	switch (ttype(v)) {
	case KTAP_TNIL:
		kp_printf(ks, "nil");
		break;
	case KTAP_TNUMBER:
		kp_printf(ks, "%d", nvalue(v));
		break;
	case KTAP_TBOOLEAN:
		kp_printf(ks, "%s", (bvalue(v) == 1) ? "true" : "false");
		break;
	case KTAP_TLIGHTUSERDATA:
		kp_printf(ks, "%d", pvalue(v));
		break;
	case KTAP_TLCF:
		kp_printf(ks, "0x%x", fvalue(v));
		break;
	case KTAP_TSHRSTR:
	case KTAP_TLNGSTR:
		kp_printf(ks, "\"%s\"", getstr(rawtsvalue(v)));
		break;
	case KTAP_TUSERDATA:
		kp_printf(ks, "%d", uvalue(v));
		break;
	case KTAP_TTABLE:
		kp_table_dump(ks, hvalue(v));
		break;
#ifdef __KERNEL__
	case KTAP_TEVENT:
		kp_transport_event_write(ks, evalue(v));
		break;
#endif
        default:
		kp_printf(ks, "[unknown value type: %d]", ttype(v));
		break;
	}
}


/*
 * equality of ktap values. ks == NULL means raw equality
 */
int kp_equalobjv(ktap_state *ks, const ktap_value *t1, const ktap_value *t2)
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
		return kp_tstring_eqlngstr(rawtsvalue(t1), rawtsvalue(t2));
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

/*
 * ktap will not use lua's length operator on table meaning,
 * also # is not for length operator any more in ktap.
 *
 * Quote from lua mannal:
 * 2.5.5 - The Length Operator
 *
 * The length operator is denoted by the unary operator #.
 * The length of a string is its number of bytes(that is,
 * the usual meaning of string length when each character is one byte).
 *
 * The length of a table t is defined to be any integer index n
 * such that t[n] is not nil and t[n+1] is nil; moreover, if t[1] is nil,
 * n can be zero. For a regular array, with non-nil values from 1 to a given n,
 * its length is exactly that n, the index of its last value. If the array has
 * "holes" (that is, nil values between other non-nil values), then #t can be
 * any of the indices that directly precedes a nil value
 * (that is, it may consider any such nil value as the end of the array).
 */
int kp_objlen(ktap_state *ks, const ktap_value *v)
{
	switch(v->type) {
	case KTAP_TTABLE:
		return kp_table_length(ks, hvalue(v));
	case KTAP_TSTRING:
		return rawtsvalue(v)->tsv.len;
	default:
		kp_printf(ks, "cannot get length of type %d\n", v->type);
		return -1;
	}
	return 0;
}


/* need to protect allgc field? */
ktap_gcobject *kp_newobject(ktap_state *ks, int type, size_t size, ktap_gcobject **list)
{
	ktap_gcobject *o;

	o = kp_malloc(ks, sizeof(ktap_gcobject) + size);
	if (list == NULL)
		list = &G(ks)->allgc;

	gch(o)->tt = type;
	gch(o)->marked = 0;
	gch(o)->next = *list;
	*list = o;

	return o;
}

Upval *kp_newupval(ktap_state *ks)
{
	Upval *uv;

	uv = &kp_newobject(ks, KTAP_TUPVAL, sizeof(Upval), NULL)->uv;
	uv->v = &uv->u.value;
	setnilvalue(uv->v);
	return uv;
}


ktap_closure *kp_newlclosure(ktap_state *ks, int n)
{
	ktap_closure *cl;

	cl = (ktap_closure *)kp_newobject(ks, KTAP_TLCL, sizeof(*cl), NULL);
	cl->l.p = NULL;
	cl->l.nupvalues = n;
	while (n--)
		cl->l.upvals[n] = NULL;

	return cl;
}

static void free_proto(ktap_state *ks, ktap_proto *f)
{
	kp_free(ks, f->code);
	kp_free(ks, f->p);
	kp_free(ks, f->k);
	kp_free(ks, f->lineinfo);
	kp_free(ks, f->locvars);
	kp_free(ks, f->upvalues);
	kp_free(ks, f);
}

ktap_proto *kp_newproto(ktap_state *ks)
{
	ktap_proto *f;
	f = (ktap_proto *)kp_newobject(ks, KTAP_TPROTO, sizeof(*f), NULL);
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

static ktap_udata *newudata(ktap_state *ks, size_t s)
{
	ktap_udata *u;

	u = &kp_newobject(ks, KTAP_TUSERDATA, sizeof(ktap_udata) + s, NULL)->u;
	u->uv.len = s;
	return u;
}

void *kp_newuserdata(ktap_state *ks, size_t size)
{
	ktap_udata *u;

	u = newudata(ks, size);
	return u + 1;
}


void kp_free_all_gcobject(ktap_state *ks)
{
	ktap_gcobject *o = G(ks)->allgc;
	ktap_gcobject *next;

	while (o) {
		next = gch(o)->next;
		switch (gch(o)->tt) {
		case KTAP_TTABLE:
			kp_table_free(ks, (ktap_table *)o);
			break;
		case KTAP_TPROTO:
			free_proto(ks, (ktap_proto *)o);
			break;
		default:
			kp_free(ks, o);
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
void kp_header(u8 *h)
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


