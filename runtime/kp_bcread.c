/*
 * Bytecode reader.
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

#include "../include/ktap_types.h"
#include "../include/ktap_bc.h"
#include "../include/ktap_err.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_tab.h"


/* Context for bytecode reader. */
typedef struct BCReadCtx {
	ktap_state_t *ks;
	int flags;
	char *start;
	char *p;
	char *pe;
	ktap_str_t *chunkname;
	ktap_val_t *savetop;
} BCReadCtx;


#define bcread_flags(ctx)	(ctx)->flags
#define bcread_swap(ctx) \
	((bcread_flags(ctx) & BCDUMP_F_BE) != KP_BE*BCDUMP_F_BE)
#define bcread_oldtop(ctx)		(ctx)->savetop
#define bcread_savetop(ctx)	(ctx)->savetop = (ctx)->ks->top;

static inline uint32_t bswap(uint32_t x)
{
	return (uint32_t)__builtin_bswap32((int32_t)x);
}

/* -- Input buffer handling ----------------------------------------------- */

/* Throw reader error. */
static void bcread_error(BCReadCtx *ctx, ErrMsg em)
{
	kp_error(ctx->ks, "%s\n", err2msg(em));
}

/* Return memory block from buffer. */
static inline uint8_t *bcread_mem(BCReadCtx *ctx, int len)
{
	uint8_t *p = (uint8_t *)ctx->p;
	ctx->p += len;
	kp_assert(ctx->p <= ctx->pe);
	return p;
}

/* Copy memory block from buffer. */
static void bcread_block(BCReadCtx *ctx, void *q, int len)
{
	memcpy(q, bcread_mem(ctx, len), len);
}

/* Read byte from buffer. */
static inline uint32_t bcread_byte(BCReadCtx *ctx)
{
	kp_assert(ctx->p < ctx->pe);
	return (uint32_t)(uint8_t)*ctx->p++;
}

/* Read ULEB128 value from buffer. */
static inline uint32_t bcread_uint32(BCReadCtx *ctx)
{
	uint32_t v;
	bcread_block(ctx, &v, sizeof(uint32_t));
	kp_assert(ctx->p <= ctx->pe);
	return v;
}

/* -- Bytecode reader ----------------------------------------------------- */

/* Read debug info of a prototype. */
static void bcread_dbg(BCReadCtx *ctx, ktap_proto_t *pt, int sizedbg)
{
	void *lineinfo = (void *)proto_lineinfo(pt);

	bcread_block(ctx, lineinfo, sizedbg);
	/* Swap lineinfo if the endianess differs. */
	if (bcread_swap(ctx) && pt->numline >= 256) {
		int i, n = pt->sizebc-1;
		if (pt->numline < 65536) {
			uint16_t *p = (uint16_t *)lineinfo;
			for (i = 0; i < n; i++)
				p[i] = (uint16_t)((p[i] >> 8)|(p[i] << 8));
		} else {
			uint32_t *p = (uint32_t *)lineinfo;
			for (i = 0; i < n; i++)
				p[i] = bswap(p[i]);
		}
	}
}

/* Find pointer to varinfo. */
static const void *bcread_varinfo(ktap_proto_t *pt)
{
	const uint8_t *p = proto_uvinfo(pt);
	int n = pt->sizeuv;
	if (n)
		while (*p++ || --n) ;
	return p;
}

/* Read a single constant key/value of a template table. */
static int bcread_ktabk(BCReadCtx *ctx, ktap_val_t *o)
{
	int tp = bcread_uint32(ctx);
	if (tp >= BCDUMP_KTAB_STR) {
		int len = tp - BCDUMP_KTAB_STR;
		const char *p = (const char *)bcread_mem(ctx, len);
		ktap_str_t *ts = kp_str_new(ctx->ks, p, len);
		if (unlikely(!ts))
			return -ENOMEM;

		set_string(o, ts);
	} else if (tp == BCDUMP_KTAB_NUM) {
		set_number(o, *(ktap_number *)bcread_mem(ctx,
					sizeof(ktap_number)));
	} else {
 		kp_assert(tp <= BCDUMP_KTAB_TRUE);
		setitype(o, ~tp);
	}
	return 0;
}

/* Read a template table. */
static ktap_tab_t *bcread_ktab(BCReadCtx *ctx)
{
	int narray = bcread_uint32(ctx);
	int nhash = bcread_uint32(ctx);

	ktap_tab_t *t = kp_tab_new(ctx->ks, narray, hsize2hbits(nhash));
	if (!t)
		return NULL;

	if (narray) {  /* Read array entries. */
		int i;
		ktap_val_t *o = t->array;
		for (i = 0; i < narray; i++, o++) 
			if (bcread_ktabk(ctx, o))
				return NULL;
	}
	if (nhash) {  /* Read hash entries. */
		int i;
		for (i = 0; i < nhash; i++) {
			ktap_val_t key;
			ktap_val_t val;
			if (bcread_ktabk(ctx, &key))
				return NULL;
			kp_assert(!is_nil(&key));
			if (bcread_ktabk(ctx, &val))
				return NULL;
			kp_tab_set(ctx->ks, t, &key, &val);
		}
	}
	return t;
}

/* Read GC constants(string, table, child proto) of a prototype. */
static int bcread_kgc(BCReadCtx *ctx, ktap_proto_t *pt, int sizekgc)
{
	ktap_obj_t **kr = (ktap_obj_t **)pt->k - (ptrdiff_t)sizekgc;
	int i;

	for (i = 0; i < sizekgc; i++, kr++) {
		int tp = bcread_uint32(ctx);
		if (tp >= BCDUMP_KGC_STR) {
			int len = tp - BCDUMP_KGC_STR;
			const char *p = (const char *)bcread_mem(ctx, len);
			*kr =(ktap_obj_t *)kp_str_new(ctx->ks, p, len);
			if (unlikely(!*kr))
				return -1;
		} else if (tp == BCDUMP_KGC_TAB) {
			*kr = (ktap_obj_t *)bcread_ktab(ctx);
			if (unlikely(!*kr))
				return -1;
		} else if (tp == BCDUMP_KGC_CHILD){
			ktap_state_t *ks = ctx->ks;
			if (ks->top <= bcread_oldtop(ctx)) {
				bcread_error(ctx, KP_ERR_BCBAD);
				return -1;
			}
			ks->top--;
			*kr = (ktap_obj_t *)ptvalue(ks->top);
		} else {
			bcread_error(ctx, KP_ERR_BCBAD);
			return -1;
		}
	}

	return 0;
}

/* Read number constants of a prototype. */
static void bcread_knum(BCReadCtx *ctx, ktap_proto_t *pt, int sizekn)
{
	int i;
	ktap_val_t *o = pt->k;

	for (i = 0; i < sizekn; i++, o++) {
		set_number(o, *(ktap_number *)bcread_mem(ctx,
					sizeof(ktap_number)));
	}
}

/* Read bytecode instructions. */
static void bcread_bytecode(BCReadCtx *ctx, ktap_proto_t *pt, int sizebc)
{
	BCIns *bc = proto_bc(pt);
	bc[0] = BCINS_AD((pt->flags & PROTO_VARARG) ? BC_FUNCV : BC_FUNCF,
			  pt->framesize, 0);
	bcread_block(ctx, bc+1, (sizebc-1)*(int)sizeof(BCIns));
	/* Swap bytecode instructions if the endianess differs. */
	if (bcread_swap(ctx)) {
		int i;
		for (i = 1; i < sizebc; i++) bc[i] = bswap(bc[i]);
	}
}

/* Read upvalue refs. */
static void bcread_uv(BCReadCtx *ctx, ktap_proto_t *pt, int sizeuv)
{
	if (sizeuv) {
		uint16_t *uv = proto_uv(pt);
		bcread_block(ctx, uv, sizeuv*2);
		/* Swap upvalue refs if the endianess differs. */
		if (bcread_swap(ctx)) {
			int i;
			for (i = 0; i < sizeuv; i++)
				uv[i] = (uint16_t)((uv[i] >> 8)|(uv[i] << 8));
		}
	}
}

/* Read a prototype. */
static ktap_proto_t *bcread_proto(BCReadCtx *ctx)
{
	ktap_proto_t *pt;
	int framesize, numparams, flags;
	int sizeuv, sizekgc, sizekn, sizebc, sizept;
	int ofsk, ofsuv, ofsdbg;
	int sizedbg = 0;
	BCLine firstline = 0, numline = 0;

	/* Read prototype header. */
	flags = bcread_byte(ctx);
	numparams = bcread_byte(ctx);
	framesize = bcread_byte(ctx);
	sizeuv = bcread_byte(ctx);
	sizekgc = bcread_uint32(ctx);
	sizekn = bcread_uint32(ctx);
	sizebc = bcread_uint32(ctx) + 1;
	if (!(bcread_flags(ctx) & BCDUMP_F_STRIP)) {
		sizedbg = bcread_uint32(ctx);
		if (sizedbg) {
			firstline = bcread_uint32(ctx);
			numline = bcread_uint32(ctx);
		}
	}

	/* Calculate total size of prototype including all colocated arrays. */
	sizept = (int)sizeof(ktap_proto_t) + sizebc * (int)sizeof(BCIns) +
			sizekgc * (int)sizeof(ktap_obj_t *);
	sizept = (sizept + (int)sizeof(ktap_val_t)-1) &
			~((int)sizeof(ktap_val_t)-1);
	ofsk = sizept; sizept += sizekn*(int)sizeof(ktap_val_t);
	ofsuv = sizept; sizept += ((sizeuv+1)&~1)*2;
	ofsdbg = sizept; sizept += sizedbg;

	/* Allocate prototype object and initialize its fields. */
	pt = (ktap_proto_t *)kp_obj_new(ctx->ks, (int)sizept);
	pt->gct = ~KTAP_TPROTO;
	pt->numparams = (uint8_t)numparams;
	pt->framesize = (uint8_t)framesize;
	pt->sizebc = sizebc;
	pt->k = (char *)pt + ofsk;
	pt->uv = (char *)pt + ofsuv;
	pt->sizekgc = 0;  /* Set to zero until fully initialized. */
	pt->sizekn = sizekn;
	pt->sizept = sizept;
	pt->sizeuv = (uint8_t)sizeuv;
	pt->flags = (uint8_t)flags;
	pt->chunkname = ctx->chunkname;

	/* Close potentially uninitialized gap between bc and kgc. */
	*(uint32_t *)((char *)pt + ofsk - sizeof(ktap_obj_t *)*(sizekgc+1))
									= 0;

	/* Read bytecode instructions and upvalue refs. */
	bcread_bytecode(ctx, pt, sizebc);
	bcread_uv(ctx, pt, sizeuv);

	/* Read constants. */
	if (bcread_kgc(ctx, pt, sizekgc))
		return NULL;
	pt->sizekgc = sizekgc;
	bcread_knum(ctx, pt, sizekn);

	/* Read and initialize debug info. */
	pt->firstline = firstline;
	pt->numline = numline;
	if (sizedbg) {
		int sizeli = (sizebc-1) << (numline < 256 ? 0 :
					numline < 65536 ? 1 : 2);
		pt->lineinfo = (char *)pt + ofsdbg;
		pt->uvinfo = (char *)pt + ofsdbg + sizeli;
		bcread_dbg(ctx, pt, sizedbg);
		pt->varinfo = (void *)bcread_varinfo(pt);
	} else {
		pt->lineinfo = NULL;
		pt->uvinfo = NULL;
		pt->varinfo = NULL;
	}
	return pt;
}

/* Read and check header of bytecode dump. */
static int bcread_header(BCReadCtx *ctx)
{
	uint32_t flags;

	if (bcread_byte(ctx) != BCDUMP_HEAD1 ||
		bcread_byte(ctx) != BCDUMP_HEAD2 ||
		bcread_byte(ctx) != BCDUMP_HEAD3 ||
		bcread_byte(ctx) != BCDUMP_VERSION)
		return -1;

	bcread_flags(ctx) = flags = bcread_byte(ctx);

	if ((flags & ~(BCDUMP_F_KNOWN)) != 0)
		return -1;

	if ((flags & BCDUMP_F_FFI)) {
		return -1;
	}

	if ((flags & BCDUMP_F_STRIP)) {
		ctx->chunkname = kp_str_newz(ctx->ks, "striped");
	} else {
		int len = bcread_uint32(ctx);
		ctx->chunkname = kp_str_new(ctx->ks,
				(const char *)bcread_mem(ctx, len), len);
	}

	if (unlikely(!ctx->chunkname))
		return -1;

	return 0;
}

/* Read a bytecode dump. */
ktap_proto_t *kp_bcread(ktap_state_t *ks, unsigned char *buff, int len)
{
	BCReadCtx ctx;

	ctx.ks = ks;
	ctx.p = buff;
	ctx.pe = buff + len;

	ctx.start = buff;

	bcread_savetop(&ctx);
	/* Check for a valid bytecode dump header. */
	if (bcread_header(&ctx)) {
		bcread_error(&ctx, KP_ERR_BCFMT);
		return NULL;
	}

	for (;;) {  /* Process all prototypes in the bytecode dump. */
		ktap_proto_t *pt;
		int len;
		const char *startp;
		/* Read length. */
		if (ctx.p < ctx.pe && ctx.p[0] == 0) {  /* Shortcut EOF. */
			ctx.p++;
			break;
		}
		len = bcread_uint32(&ctx);
		if (!len)
			break;  /* EOF */
		startp = ctx.p;
		pt = bcread_proto(&ctx);
		if (!pt)
			return NULL;
		if (ctx.p != startp + len) {
			bcread_error(&ctx, KP_ERR_BCBAD);
			return NULL;
		}
		set_proto(ks->top, pt);
		incr_top(ks);
	}
	if ((int32_t)(2*(uint32_t)(ctx.pe - ctx.p)) > 0 ||
			ks->top-1 != bcread_oldtop(&ctx)) {
		bcread_error(&ctx, KP_ERR_BCBAD);
		return NULL;
	}

	/* Pop off last prototype. */
	ks->top--;
	return ptvalue(ks->top);
}

