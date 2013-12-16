/*
 * dump.c - save precompiled ktap chunks
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/ktap_types.h"
#include "ktapc.h"
#include "cparser.h"


typedef struct {
	ktap_writer writer;
	void *data;
	int strip;
	int status;
} DumpState;

#define DumpMem(b, n, size, D)	DumpBlock(b, (n)*(size), D)
#define DumpVar(x, D)		DumpMem(&x, 1, sizeof(x), D)

static void DumpBlock(const void *b, size_t size, DumpState *D)
{
	if (D->status == 0)
		D->status = ((D->writer))(b, size, D->data);
}

static void DumpChar(int y, DumpState *D)
{
	char x = (char)y;
	DumpVar(x, D);
}

static void DumpInt(int x, DumpState *D)
{
	DumpVar(x, D);
}

static void DumpNumber(ktap_number x, DumpState *D)
{
	DumpVar(x,D);
}

static void DumpVector(const void *b, int n, size_t size, DumpState *D)
{
	DumpInt(n, D);
	DumpMem(b, n, size, D);
}

static void DumpString(const ktap_string *s, DumpState *D)
{
	if (s == NULL) {
		int size = 0;
		DumpVar(size, D);
	} else {
		int size = s->tsv.len + 1; /* include trailing '\0' */
		DumpVar(size, D);
		DumpBlock(getstr(s), size * sizeof(char), D);
	}
}

#define DumpCode(f, D)	 DumpVector(f->code, f->sizecode, sizeof(ktap_instruction), D)

static void DumpFunction(const ktap_proto *f, DumpState *D);

static void DumpConstants(const ktap_proto *f, DumpState *D)
{
	int i, n = f->sizek;

	DumpInt(n, D);
	for (i = 0; i < n; i++) {
		const ktap_value* o=&f->k[i];
		DumpChar(ttypenv(o), D);
		switch (ttypenv(o)) {
		case KTAP_TNIL:
			break;
		case KTAP_TBOOLEAN:
			DumpChar(bvalue(o), D);
			break;
		case KTAP_TNUMBER:
			DumpNumber(nvalue(o), D);
			break;
		case KTAP_TSTRING:
			DumpString(rawtsvalue(o), D);
			break;
		default:
			printf("ktap: DumpConstants with unknown vaule type %d\n", ttypenv(o));
			ktap_assert(0);
		}
	}
	n = f->sizep;
	DumpInt(n, D);
	for (i = 0; i < n; i++)
		DumpFunction(f->p[i], D);
}

static void DumpUpvalues(const ktap_proto *f, DumpState *D)
{
	int i, n = f->sizeupvalues;

	DumpInt(n, D);
	for (i = 0; i < n; i++) {
		DumpChar(f->upvalues[i].instack, D);
		DumpChar(f->upvalues[i].idx, D);
	}
}

static void DumpDebug(const ktap_proto *f, DumpState *D)
{
	int i,n;

	DumpString((D->strip) ? NULL : f->source, D);
	n= (D->strip) ? 0 : f->sizelineinfo;
	DumpVector(f->lineinfo, n, sizeof(int), D);
	n = (D->strip) ? 0 : f->sizelocvars;
	DumpInt(n, D);

	for (i = 0; i < n; i++) {
		DumpString(f->locvars[i].varname, D);
		DumpInt(f->locvars[i].startpc, D);
		DumpInt(f->locvars[i].endpc, D);
	}
	n = (D->strip) ? 0 : f->sizeupvalues;
	DumpInt(n, D);
	for (i = 0; i < n; i++)
		DumpString(f->upvalues[i].name, D);
}

static void DumpFunction(const ktap_proto *f, DumpState *D)
{
	DumpInt(f->linedefined, D);
	DumpInt(f->lastlinedefined, D);
	DumpChar(f->numparams, D);
	DumpChar(f->is_vararg, D);
	DumpChar(f->maxstacksize, D);
	DumpCode(f, D);
	DumpConstants(f, D);
	DumpUpvalues(f, D);
	DumpDebug(f, D);
}

static void DumpHeader(DumpState *D)
{
	u8 h[KTAPC_HEADERSIZE];

	kp_header(h);
	DumpBlock(h, KTAPC_HEADERSIZE, D);
}

#ifdef CONFIG_KTAP_FFI
static void DumpCSymbolFunc(csymbol *cs, DumpState *D)
{
	csymbol_func *csf = csym_func(cs);

	DumpBlock(cs, sizeof(csymbol), D);
	/* dump csymbol index for argument types */
	DumpBlock(csf->arg_ids, csf->arg_nr*sizeof(int), D);
}

static void DumpCSymbolStruct(csymbol *cs, DumpState *D)
{
	csymbol_struct *csst = csym_struct(cs);

	DumpBlock(cs, sizeof(csymbol), D);
	/* dump csymbol index for argument types */
	DumpBlock(csst->members, csst->memb_nr*sizeof(struct_member), D);
}

static void DumpCSymbols(DumpState *D)
{
	int i, cs_nr;
	cp_csymbol_state *cs_state;
	csymbol *cs, *cs_arr;

	cs_state = ctype_get_csym_state();
	cs_arr = cs_state->cs_arr;
	cs_nr = cs_state->cs_nr;

	if (!cs_arr || cs_nr == 0) {
		DumpInt(0, D);
		return;
	}

	/* dump number of csymbols */
	DumpInt(cs_nr, D);
	/* dump size of csymbol, for safty check in vm */
	DumpInt(sizeof(csymbol), D);
	for (i = 0; i < cs_nr; i++) {
		cs = &cs_arr[i];
		switch (cs->type) {
		case FFI_FUNC:
			DumpCSymbolFunc(cs, D);
			break;
		case FFI_STRUCT:
		case FFI_UNION:
			DumpCSymbolStruct(cs, D);
			break;
		default:
			DumpBlock(cs, sizeof(csymbol), D);
			break;
		}
	}
}
#else
static void DumpCSymbols(DumpState *D)
{
	/* always dump zero when FFI is disabled */
	DumpInt(0, D);
}
#endif /* CONFIG_KTAP_FFI */

/*
 * dump ktap function as precompiled chunk
 */
int ktapc_dump(const ktap_proto *f, ktap_writer w, void *data, int strip)
{
	DumpState D;

	D.writer = w;
	D.data = data;
	D.strip = strip;
	D.status = 0;
	DumpHeader(&D);
	DumpCSymbols(&D);
	DumpFunction(f, &D);
	return D.status;
}


/*******************************************************************/

#define print_base(i) \
	do {	\
		if (i < f->sizelocvars) /* it's a localvars */ \
			printf("%s", getstr(f->locvars[i].varname));  \
		else \
			printf("base + %d", i);	\
	} while (0)

#define print_RK(instr, _field)  \
	do {	\
		if (ISK(GETARG_##_field(instr))) \
			ktapc_showobj(k + INDEXK(GETARG_##_field(instr))); \
		else \
			print_base(GETARG_##_field(instr)); \
	} while (0)

#define print_RKA(instr) print_RK(instr, A)
#define print_RKB(instr) print_RK(instr, B)
#define print_RKC(instr) print_RK(instr, C)

#define print_upvalue(idx) \
	do {	\
		if ((idx) == 0) \
			printf("global"); \
		else \
			printf("upvalues[%d]", (idx)); \
	} while (0)

static void decode_instruction(ktap_proto *f, int instr)
{
	int opcode = GET_OPCODE(instr);
	ktap_value *k;

	k = f->k;

	printf("%.8x\t", instr);
	printf("%s\t", ktap_opnames[opcode]);

	switch (opcode) {
	case OP_MOVE:
		printf("\t");
		print_base(GETARG_A(instr));
		printf(" <- ");
		print_base(GETARG_B(instr));
		break;
	case OP_GETTABUP:
		print_base(GETARG_A(instr));
		printf(" <- ");
		print_upvalue(GETARG_B(instr));
		printf("{"); print_RKC(instr); printf("}");

		break;
	case OP_GETTABLE:
		print_base(GETARG_A(instr));
		printf(" <- ");

		print_base(GETARG_B(instr));

		printf("{");
		print_RKC(instr);
		printf("}");
		break;
	case OP_SETTABLE:
		print_base(GETARG_A(instr));
		printf("{");
		print_RKB(instr);
		printf("}");
		printf(" <- ");
		print_RKC(instr);
		break;
	case OP_LOADK:
		printf("\t");
		print_base(GETARG_A(instr));
		printf(" <- ");

		ktapc_showobj(k + GETARG_Bx(instr));
		break;
	case OP_CALL:
		printf("\t");
		print_base(GETARG_A(instr));
		break;
	case OP_JMP:
		printf("\t%d", GETARG_sBx(instr));
		break;
	case OP_CLOSURE:
		printf("\t");
		print_base(GETARG_A(instr));
		printf(" <- closure(func starts from line %d)",
			f->p[GETARG_Bx(instr)]->lineinfo[0]);
		break;
	case OP_SETTABUP:
		print_upvalue(GETARG_A(instr));
		printf("{");
		print_RKB(instr);
		printf("} <- ");

		print_RKC(instr);
		break;
	case OP_GETUPVAL:
		print_base(GETARG_A(instr));
		printf(" <- ");

		print_upvalue(GETARG_B(instr));
		break;
	case OP_NEWTABLE:
		print_base(GETARG_A(instr));
		printf(" <- {}");
	default:
		break;
	}

	printf("\n");
}

static int function_nr = 0;

#ifdef CONFIG_KTAP_FFI
void ktapc_dump_csymbol_id(char *prefix, int id)
{
	csymbol *cs, *cs_arr;

	cs_arr = ctype_get_csym_state()->cs_arr;
	cs = &cs_arr[id];
	if (prefix != NULL)
		printf("%s: ", prefix);
	printf("%s (id: %d; ffi_type: %s)\n", cs->name,
			id, ffi_type_name(cs->type));
}

/* this is a debug function used for check csymbol array */
void ktapc_dump_csymbols()
{
	int i, j, cs_nr;
	cp_csymbol_state *cs_state;
	csymbol *cs, *cs_arr;
	csymbol_func *csf;
	csymbol_struct *csst;

	cs_state = ctype_get_csym_state();
	cs_arr = cs_state->cs_arr;
	cs_nr = cs_state->cs_nr;

	printf("\n----------------------------------------------------\n");
	printf("Number of csymbols: %d\n", cs_nr);

	for (i = 0; i < cs_nr; i++) {
		cs = &cs_arr[i];
		printf("%dth symbol", i);
		ktapc_dump_csymbol_id("", i);
		switch (cs->type) {
		case FFI_PTR:
			ktapc_dump_csymbol_id("\tDeref", csym_ptr_deref_id(cs));
			break;
		case FFI_FUNC:
			csf = csym_func(cs);
			printf("\tAddress: 0x%p\n", csf->addr);
			ktapc_dump_csymbol_id("\tReturn", csf->ret_id);
			printf("\tArg number: %d\n", csf->arg_nr);
			for (j = 0; j < csf->arg_nr; j++)
				ktapc_dump_csymbol_id("\t\tArg", csf->arg_ids[j]);
			printf("\tHas variable arg: %d\n", csf->has_var_arg);
			break;
		case FFI_STRUCT:
		case FFI_UNION:
			csst = csym_struct(cs);
			printf("\tMember number: %d\n", csst->memb_nr);
			for (j = 0; j < csst->memb_nr; j++) {
				printf("\t\tMember %s", csst->members[j].name);
				ktapc_dump_csymbol_id("", csst->members[j].id);
			}
			break;
		default:
			break;
		}
	}
}
#endif

/* this is a debug function used for check bytecode chunk file */
void ktapc_dump_function(int level, ktap_proto *f)
{
	int i;

	printf("\n----------------------------------------------------\n");
	printf("function %d [level %d]:\n", function_nr++, level);
	printf("linedefined: %d\n", f->linedefined);
	printf("lastlinedefined: %d\n", f->lastlinedefined);
	printf("numparams: %d\n", f->numparams);
	printf("is_vararg: %d\n", f->is_vararg);
	printf("maxstacksize: %d\n", f->maxstacksize);
	printf("source: %s\n", getstr(f->source));
	printf("sizelineinfo: %d \t", f->sizelineinfo);
	for (i = 0; i < f->sizelineinfo; i++)
		printf("%d ", f->lineinfo[i]);
	printf("\n");

	printf("sizek: %d\n", f->sizek);
	for (i = 0; i < f->sizek; i++) {
		switch(f->k[i].type) {
		case KTAP_TNIL:
			printf("\tNIL\n");
			break;
		case KTAP_TBOOLEAN:
			printf("\tBOOLEAN: ");
			printf("%d\n", f->k[i].val.b);
			break;
		case KTAP_TNUMBER:
			printf("\tTNUMBER: ");
			printf("%ld\n", f->k[i].val.n);
			break;
		case KTAP_TSHRSTR:
		case KTAP_TLNGSTR:
			printf("\tTSTRING: ");
			printf("%s\n", svalue(&(f->k[i])));
			break;
		default:
			printf("\tUnknow constant type %d: ", f->k[i].type);
			ktapc_showobj(&(f->k[i]));
			printf("\n");
		}
	}

	printf("sizelocvars: %d\n", f->sizelocvars);
	for (i = 0; i < f->sizelocvars; i++) {
		printf("\tlocvars: %s startpc: %d endpc: %d\n",
			getstr(f->locvars[i].varname), f->locvars[i].startpc,
			f->locvars[i].endpc);
	}

	printf("sizeupvalues: %d\n", f->sizeupvalues);
	for (i = 0; i < f->sizeupvalues; i++) {
		printf("\tname: %s instack: %d idx: %d\n",
			getstr(f->upvalues[i].name), f->upvalues[i].instack,
			f->upvalues[i].idx);
	}

	printf("\n");
	printf("sizecode: %d\n", f->sizecode);
	for (i = 0; i < f->sizecode; i++)
		decode_instruction(f, f->code[i]);

	printf("sizep: %d\n", f->sizep);
	for (i = 0; i < f->sizep; i++)
		ktapc_dump_function(level + 1, f->p[i]);

}

