/*
 * Bytecode writer
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2014 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/ktap_types.h"
#include "kp_util.h"

/* Context for bytecode writer. */
typedef struct BCWriteCtx {
	SBuf sb;		/* Output buffer. */
	ktap_proto_t *pt;	/* Root prototype. */
	ktap_writer wfunc;	/* Writer callback. */
	void *wdata;		/* Writer callback data. */
	int strip;		/* Strip debug info. */
	int status;		/* Status from writer callback. */
} BCWriteCtx;


static char *bcwrite_uint32(char *p, uint32_t v)
{
	memcpy(p, &v, sizeof(uint32_t));
	p += sizeof(uint32_t);
	return p;
}

/* -- Bytecode writer ----------------------------------------------------- */

/* Write a single constant key/value of a template table. */
static void bcwrite_ktabk(BCWriteCtx *ctx, const ktap_val_t *o, int narrow)
{
	char *p = kp_buf_more(&ctx->sb, 1+10);
	if (is_string(o)) {
		const ktap_str_t *str = rawtsvalue(o);
		int len = str->len;
		p = kp_buf_more(&ctx->sb, 5+len);
		p = bcwrite_uint32(p, BCDUMP_KTAB_STR+len);
		p = kp_buf_wmem(p, getstr(str), len);
	} else if (is_number(o)) {
		p = bcwrite_uint32(p, BCDUMP_KTAB_NUM);
		p = kp_buf_wmem(p, &nvalue(o), sizeof(ktap_number));
	} else {
		kp_assert(tvispri(o));
		p = bcwrite_uint32(p, BCDUMP_KTAB_NIL+~itype(o));
	}
	setsbufP(&ctx->sb, p);
}

/* Write a template table. */
static void bcwrite_ktab(BCWriteCtx *ctx, char *p, const ktap_tab_t *t)
{
	int narray = 0, nhash = 0;
	if (t->asize > 0) {  /* Determine max. length of array part. */
		ptrdiff_t i;
		ktap_val_t *array = t->array;
		for (i = (ptrdiff_t)t->asize-1; i >= 0; i--)
			if (!is_nil(&array[i]))
				break;
			narray = (int)(i+1);
	}
	if (t->hmask > 0) {  /* Count number of used hash slots. */
		int i, hmask = t->hmask;
		ktap_node_t *node = t->node;
		for (i = 0; i <= hmask; i++)
			nhash += !is_nil(&node[i].val);
	}
	/* Write number of array slots and hash slots. */
	p = bcwrite_uint32(p, narray);
	p = bcwrite_uint32(p, nhash);
	setsbufP(&ctx->sb, p);
	if (narray) {  /* Write array entries (may contain nil). */
		int i;
		ktap_val_t *o = t->array;
		for (i = 0; i < narray; i++, o++)
			bcwrite_ktabk(ctx, o, 1);
	}
	if (nhash) {  /* Write hash entries. */
		int i = nhash;
		ktap_node_t *node = t->node + t->hmask;
		for (;; node--)
			if (!is_nil(&node->val)) {
				bcwrite_ktabk(ctx, &node->key, 0);
				bcwrite_ktabk(ctx, &node->val, 1);
				if (--i == 0)
					break;
			}
	}
}

/* Write GC constants of a prototype. */
static void bcwrite_kgc(BCWriteCtx *ctx, ktap_proto_t *pt)
{
	int i, sizekgc = pt->sizekgc;
	ktap_obj_t **kr = (ktap_obj_t **)pt->k - (ptrdiff_t)sizekgc;

	for (i = 0; i < sizekgc; i++, kr++) {
		ktap_obj_t *o = *kr;
		int tp, need = 1;
		char *p;

		/* Determine constant type and needed size. */
		if (o->gch.gct == ~KTAP_TSTR) {
			tp = BCDUMP_KGC_STR + ((ktap_str_t *)o)->len;
			need = 5 + ((ktap_str_t *)o)->len;
		} else if (o->gch.gct == ~KTAP_TPROTO) {
			kp_assert((pt->flags & PROTO_CHILD));
			tp = BCDUMP_KGC_CHILD;
		} else {
			kp_assert(o->gch.gct == ~KTAP_TTAB);
			tp = BCDUMP_KGC_TAB;
			need = 1+2*5;
		}

		/* Write constant type. */
		p = kp_buf_more(&ctx->sb, need);
		p = bcwrite_uint32(p, tp);
		/* Write constant data (if any). */
		if (tp >= BCDUMP_KGC_STR) {
			p = kp_buf_wmem(p, getstr((ktap_str_t *)o),
					((ktap_str_t *)o)->len);
		} else if (tp == BCDUMP_KGC_TAB) {
			bcwrite_ktab(ctx, p, (ktap_tab_t *)o);
			continue;
		}
		setsbufP(&ctx->sb, p);
	}
}

/* Write number constants of a prototype. */
static void bcwrite_knum(BCWriteCtx *ctx, ktap_proto_t *pt)
{
	int i, sizekn = pt->sizekn;
	const ktap_val_t *o = (ktap_val_t *)pt->k;
	char *p = kp_buf_more(&ctx->sb, 10*sizekn);

	for (i = 0; i < sizekn; i++, o++) {
		if (is_number(o))
			p = kp_buf_wmem(p, &nvalue(o), sizeof(ktap_number));
	}
	setsbufP(&ctx->sb, p);
}

/* Write bytecode instructions. */
static char *bcwrite_bytecode(BCWriteCtx *ctx, char *p, ktap_proto_t *pt)
{
	int nbc = pt->sizebc-1;  /* Omit the [JI]FUNC* header. */

	p = kp_buf_wmem(p, proto_bc(pt)+1, nbc*(int)sizeof(BCIns));
	return p;
}

/* Write prototype. */
static void bcwrite_proto(BCWriteCtx *ctx, ktap_proto_t *pt)
{
	int sizedbg = 0;
	char *p;

	/* Recursively write children of prototype. */
	if (pt->flags & PROTO_CHILD) {
		ptrdiff_t i, n = pt->sizekgc;
		ktap_obj_t **kr = (ktap_obj_t **)pt->k - 1;
		for (i = 0; i < n; i++, kr--) {
			ktap_obj_t *o = *kr;
			if (o->gch.gct == ~KTAP_TPROTO)
				bcwrite_proto(ctx, (ktap_proto_t *)o);
		}
	}

	/* Start writing the prototype info to a buffer. */
	p = kp_buf_need(&ctx->sb,
		5+4+6*5+(pt->sizebc-1)*(int)sizeof(BCIns)+pt->sizeuv*2);
	p += 4;  /* Leave room for final size. */

	/* Write prototype header. */
	*p++ = (pt->flags & (PROTO_CHILD|PROTO_VARARG|PROTO_FFI));
	*p++ = pt->numparams;
	*p++ = pt->framesize;
	*p++ = pt->sizeuv;
	p = bcwrite_uint32(p, pt->sizekgc);
	p = bcwrite_uint32(p, pt->sizekn);
	p = bcwrite_uint32(p, pt->sizebc-1);
	if (!ctx->strip) {
		if (proto_lineinfo(pt))
			sizedbg = pt->sizept -
				(int)((char *)proto_lineinfo(pt) - (char *)pt);
		p = bcwrite_uint32(p, sizedbg);
		if (sizedbg) {
			p = bcwrite_uint32(p, pt->firstline);
			p = bcwrite_uint32(p, pt->numline);
		}
	}

	/* Write bytecode instructions and upvalue refs. */
	p = bcwrite_bytecode(ctx, p, pt);
	p = kp_buf_wmem(p, proto_uv(pt), pt->sizeuv*2);
	setsbufP(&ctx->sb, p);

	/* Write constants. */
	bcwrite_kgc(ctx, pt);
	bcwrite_knum(ctx, pt);

	/* Write debug info, if not stripped. */
	if (sizedbg) {
		p = kp_buf_more(&ctx->sb, sizedbg);
		p = kp_buf_wmem(p, proto_lineinfo(pt), sizedbg);
		setsbufP(&ctx->sb, p);
	}

	/* Pass buffer to writer function. */
	if (ctx->status == 0) {
		int n = sbuflen(&ctx->sb) - 4;
		char *q = sbufB(&ctx->sb);
		p = bcwrite_uint32(q, n);  /* Fill in final size. */
		kp_assert(p == sbufB(&ctx->sb) + 4);
		ctx->status = ctx->wfunc(q, n + 4, ctx->wdata);
	}
}

/* Write header of bytecode dump. */
static void bcwrite_header(BCWriteCtx *ctx)
{
	ktap_str_t *chunkname = proto_chunkname(ctx->pt);
	const char *name = getstr(chunkname);
	int len = chunkname->len;
	char *p = kp_buf_need(&ctx->sb, 5+5+len);
	*p++ = BCDUMP_HEAD1;
	*p++ = BCDUMP_HEAD2;
	*p++ = BCDUMP_HEAD3;
	*p++ = BCDUMP_VERSION;
	*p++ = (ctx->strip ? BCDUMP_F_STRIP : 0) + (KP_BE ? BCDUMP_F_BE : 0);

	if (!ctx->strip) {
		p = bcwrite_uint32(p, len);
		p = kp_buf_wmem(p, name, len);
	}
	ctx->status = ctx->wfunc(sbufB(&ctx->sb),
		(int)(p - sbufB(&ctx->sb)), ctx->wdata);
}

/* Write footer of bytecode dump. */
static void bcwrite_footer(BCWriteCtx *ctx)
{
	if (ctx->status == 0) {
		uint8_t zero = 0;
		ctx->status = ctx->wfunc(&zero, 1, ctx->wdata);
	}
}

/* Write bytecode for a prototype. */
int kp_bcwrite(ktap_proto_t *pt, ktap_writer writer, void *data, int strip)
{
	BCWriteCtx ctx;

	ctx.pt = pt;
	ctx.wfunc = writer;
	ctx.wdata = data;
	ctx.strip = strip;
	ctx.status = 0;

	kp_buf_init(&ctx.sb);
	kp_buf_need(&ctx.sb, 1024);  /* Avoids resize for most prototypes. */
	bcwrite_header(&ctx);
	bcwrite_proto(&ctx, ctx.pt);
	bcwrite_footer(&ctx);

	kp_buf_free(&ctx.sb);
	return ctx.status;
}

/* -- Bytecode dump ----------------------------------------------------- */

static const char * const bc_names[] = {
#define BCNAME(name, ma, mb, mc, mt)       #name,
	BCDEF(BCNAME)
#undef BCNAME
  NULL
};

static const uint16_t bc_mode[] = {
	BCDEF(BCMODE)
};

static void dump_bytecode(ktap_proto_t *pt)
{
	int nbc = pt->sizebc - 1; /* Omit the FUNC* header. */
	BCIns *ins = proto_bc(pt) + 1;
	ktap_obj_t **kbase = pt->k;
	int i;

	printf("-- BYTECODE -- %s:%d-%d\n", getstr(pt->chunkname),
		pt->firstline, pt->firstline + pt->numline);

	for (i = 0; i < nbc; i++, ins++) {
		int op = bc_op(*ins);

		printf("%04d\t%s", i + 1, bc_names[op]);

		printf("\t%d", bc_a(*ins));
		if (bcmode_b(op) != BCMnone)
			printf("\t%d", bc_b(*ins));

		if (bcmode_hasd(op))
			printf("\t%d", bc_d(*ins));
		else
			printf("\t%d", bc_c(*ins));

		if (bcmode_b(op) == BCMstr || bcmode_c(op) == BCMstr) {
			printf("\t  ; ");
			if (bcmode_d(op) == BCMstr) {
				int idx = ~bc_d(*ins);
				printf("\"%s\"", getstr((ktap_str_t *)kbase[idx]));
			}
		}
		printf("\n");
	}
}

static int function_nr = 0;

void kp_dump_proto(ktap_proto_t *pt)
{
	printf("\n----------------------------------------------------\n");
	printf("function proto %d:\n", function_nr++);
	printf("numparams: %d\n", pt->numparams);
	printf("framesize: %d\n", pt->framesize);
	printf("sizebc: %d\n", pt->sizebc);
	printf("sizekgc: %d\n", pt->sizekgc);
	printf("sizekn: %d\n", pt->sizekn);
	printf("sizept: %d\n", pt->sizept);
	printf("sizeuv: %d\n", pt->sizeuv);
	printf("firstline: %d\n", pt->firstline);
	printf("numline: %d\n", pt->numline);

	printf("has child proto: %d\n", pt->flags & PROTO_CHILD);
	printf("has vararg: %d\n", pt->flags & PROTO_VARARG);
	printf("has ILOOP: %d\n", pt->flags & PROTO_ILOOP);

	dump_bytecode(pt);

	/* Recursively dump children of prototype. */
	if (pt->flags & PROTO_CHILD) {
		ptrdiff_t i, n = pt->sizekgc;
		ktap_obj_t **kr = (ktap_obj_t **)pt->k - 1;
		for (i = 0; i < n; i++, kr--) {
			ktap_obj_t *o = *kr;
			if (o->gch.gct == ~KTAP_TPROTO)
				kp_dump_proto((ktap_proto_t *)o);		
		}
	}
}

