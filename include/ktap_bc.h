/*
 * Bytecode instruction format.
 *
 * Copyright (C) 2012-2014 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 * Copyright (C) 2005-2014 Mike Pall.
 */

#ifndef __KTAP_BC_H__
#define __KTAP_BC_H__

#include "../include/ktap_arch.h"

/*TODO*/
#define KP_STATIC_ASSERT(cond)
#define kp_assert(cond)

/* Types for handling bytecodes. */
typedef uint32_t BCIns;  /* Bytecode instruction. */
typedef uint32_t BCPos;  /* Bytecode position. */
typedef uint32_t BCReg;  /* Bytecode register. */
typedef int32_t BCLine;  /* Bytecode line number. */

/*
 * Bytecode instruction format, 32 bit wide, fields of 8 or 16 bit:
 *
 * +----+----+----+----+
 * | B  | C  | A  | OP | Format ABC
 * +----+----+----+----+
 * |    D    | A  | OP | Format AD
 * +--------------------
 * MSB               LSB
 *
 * In-memory instructions are always stored in host byte order.
 */

/* Operand ranges and related constants. */
#define BCMAX_A		0xff
#define BCMAX_B		0xff
#define BCMAX_C		0xff
#define BCMAX_D		0xffff
#define BCBIAS_J	0x8000
#define NO_REG		BCMAX_A
#define NO_JMP		(~(BCPos)0)

/* Macros to get instruction fields. */
#define bc_op(i)	((BCOp)((i)&0xff))
#define bc_a(i)		((BCReg)(((i)>>8)&0xff))
#define bc_b(i)		((BCReg)((i)>>24))
#define bc_c(i)		((BCReg)(((i)>>16)&0xff))
#define bc_d(i)		((BCReg)((i)>>16))
#define bc_j(i)		((ptrdiff_t)bc_d(i)-BCBIAS_J)

/* Macros to set instruction fields. */
#define setbc_byte(p, x, ofs) \
	((uint8_t *)(p))[KP_ENDIAN_SELECT(ofs, 3 - ofs)] = (uint8_t)(x)
#define setbc_op(p, x)	setbc_byte(p, (x), 0)
#define setbc_a(p, x)	setbc_byte(p, (x), 1)
#define setbc_b(p, x)	setbc_byte(p, (x), 3)
#define setbc_c(p, x)	setbc_byte(p, (x), 2)
#define setbc_d(p, x) \
	((uint16_t *)(p))[KP_ENDIAN_SELECT(1, 0)] = (uint16_t)(x)
#define setbc_j(p, x)	setbc_d(p, (BCPos)((int32_t)(x)+BCBIAS_J))

/* Macros to compose instructions. */
#define BCINS_ABC(o, a, b, c) \
	(((BCIns)(o))|((BCIns)(a)<<8)|((BCIns)(b)<<24)|((BCIns)(c)<<16))
#define BCINS_AD(o, a, d) \
	(((BCIns)(o))|((BCIns)(a)<<8)|((BCIns)(d)<<16))
#define BCINS_AJ(o, a, j)	BCINS_AD(o, a, (BCPos)((int32_t)(j)+BCBIAS_J))

/*
 * Bytecode instruction definition. Order matters, see below.
 *
 * (name, filler, Amode, Bmode, Cmode or Dmode, metamethod)
 *
 * The opcode name suffixes specify the type for RB/RC or RD:
 * V = variable slot
 * S = string const
 * N = number const
 * P = primitive type (~itype)
 * B = unsigned byte literal
 * M = multiple args/results
 */
#define BCDEF(_) \
	/* Comparison ops. ORDER OPR. */ \
	_(ISLT,	var,	___,	var,	lt) \
	_(ISGE,	var,	___,	var,	lt) \
	_(ISLE,	var,	___,	var,	le) \
	_(ISGT,	var,	___,	var,	le) \
	\
	_(ISEQV,	var,	___,	var,	eq) \
	_(ISNEV,	var,	___,	var,	eq) \
	_(ISEQS,	var,	___,	str,	eq) \
	_(ISNES,	var,	___,	str,	eq) \
	_(ISEQN,	var,	___,	num,	eq) \
	_(ISNEN,	var,	___,	num,	eq) \
	_(ISEQP,	var,	___,	pri,	eq) \
	_(ISNEP,	var,	___,	pri,	eq) \
	\
	/* Unary test and copy ops. */ \
	_(ISTC,	dst,	___,	var,	___) \
	_(ISFC,	dst,	___,	var,	___) \
	_(IST,	___,	___,	var,	___) \
	_(ISF,	___,	___,	var,	___) \
	_(ISTYPE,	var,	___,	lit,	___) \
	_(ISNUM,	var,	___,	lit,	___) \
	\
	/* Unary ops. */ \
	_(MOV,	dst,	___,	var,	___) \
	_(NOT,	dst,	___,	var,	___) \
	_(UNM,	dst,	___,	var,	unm) \
	\
	/* Binary ops. ORDER OPR. VV last, POW must be next. */ \
	_(ADDVN,	dst,	var,	num,	add) \
	_(SUBVN,	dst,	var,	num,	sub) \
	_(MULVN,	dst,	var,	num,	mul) \
	_(DIVVN,	dst,	var,	num,	div) \
	_(MODVN,	dst,	var,	num,	mod) \
	\
	_(ADDNV,	dst,	var,	num,	add) \
	_(SUBNV,	dst,	var,	num,	sub) \
	_(MULNV,	dst,	var,	num,	mul) \
	_(DIVNV,	dst,	var,	num,	div) \
	_(MODNV,	dst,	var,	num,	mod) \
	\
	_(ADDVV,	dst,	var,	var,	add) \
	_(SUBVV,	dst,	var,	var,	sub) \
	_(MULVV,	dst,	var,	var,	mul) \
	_(DIVVV,	dst,	var,	var,	div) \
	_(MODVV,	dst,	var,	var,	mod) \
	\
	_(POW,	dst,	var,	var,	pow) \
	_(CAT,	dst,	rbase,	rbase,	concat) \
	\
	/* Constant ops. */ \
	_(KSTR,	dst,	___,	str,	___) \
	_(KCDATA,	dst,	___,	cdata,	___) \
	_(KSHORT,	dst,	___,	lits,	___) \
	_(KNUM,	dst,	___,	num,	___) \
	_(KPRI,	dst,	___,	pri,	___) \
	_(KNIL,	base,	___,	base,	___) \
	\
	/* Upvalue and function ops. */ \
	_(UGET,	dst,	___,	uv,	___) \
	_(USETV,	uv,	___,	var,	___) \
	_(UINCV,	uv,	___,	var,	___) \
	_(USETS,	uv,	___,	str,	___) \
	_(USETN,	uv,	___,	num,	___) \
	_(UINCN,	uv,	___,	num,	___) \
	_(USETP,	uv,	___,	pri,	___) \
	_(UCLO,	rbase,	___,	jump,	___) \
	_(FNEW,	dst,	___,	func,	gc) \
	\
	/* Table ops. */ \
	_(TNEW,	dst,	___,	lit,	gc) \
	_(TDUP,	dst,	___,	tab,	gc) \
	_(GGET,	dst,	___,	str,	index) \
	_(GSET,	var,	___,	str,	newindex) \
	_(GINC,	var,	___,	str,	newindex) \
	_(TGETV,	dst,	var,	var,	index) \
	_(TGETS,	dst,	var,	str,	index) \
	_(TGETB,	dst,	var,	lit,	index) \
	_(TGETR,	dst,	var,	var,	index) \
	_(TSETV,	var,	var,	var,	newindex) \
	_(TINCV,	var,	var,	var,	newindex) \
	_(TSETS,	var,	var,	str,	newindex) \
	_(TINCS,	var,	var,	str,	newindex) \
	_(TSETB,	var,	var,	lit,	newindex) \
	_(TINCB,	var,	var,	lit,	newindex) \
	_(TSETM,	base,	___,	num,	newindex) \
	_(TSETR,	var,	var,	var,	newindex) \
	\
	/* Calls and vararg handling. T = tail call. */ \
	_(CALLM,	base,	lit,	lit,	call) \
	_(CALL,	base,	lit,	lit,	call) \
	_(CALLMT,	base,	___,	lit,	call) \
	_(CALLT,	base,	___,	lit,	call) \
	_(ITERC,	base,	lit,	lit,	call) \
	_(ITERN,	base,	lit,	lit,	call) \
	_(VARG,	base,	lit,	lit,	___) \
	_(ISNEXT,	base,	___,	jump,	___) \
	\
	/* Returns. */ \
	_(RETM,	base,	___,	lit,	___) \
	_(RET,	rbase,	___,	lit,	___) \
	_(RET0,	rbase,	___,	lit,	___) \
	_(RET1,	rbase,	___,	lit,	___) \
	\
	/* Loops and branches. I/J = interp/JIT, I/C/L = init/call/loop. */ \
	_(FORI,	base,	___,	jump,	___) \
	_(JFORI,	base,	___,	jump,	___) \
	\
	_(FORL,	base,	___,	jump,	___) \
	_(IFORL,	base,	___,	jump,	___) \
	_(JFORL,	base,	___,	lit,	___) \
	\
	_(ITERL,	base,	___,	jump,	___) \
	_(IITERL,	base,	___,	jump,	___) \
	_(JITERL,	base,	___,	lit,	___) \
	\
	_(LOOP,	rbase,	___,	jump,	___) \
	_(ILOOP,	rbase,	___,	jump,	___) \
	_(JLOOP,	rbase,	___,	lit,	___) \
	\
	_(JMP,	rbase,	___,	jump,	___) \
	\
	/*Function headers. I/J = interp/JIT, F/V/C = fixarg/vararg/C func.*/ \
	_(FUNCF,	rbase,	___,	___,	___) \
	_(IFUNCF,	rbase,	___,	___,	___) \
	_(JFUNCF,	rbase,	___,	lit,	___) \
	_(FUNCV,	rbase,	___,	___,	___) \
	_(IFUNCV,	rbase,	___,	___,	___) \
	_(JFUNCV,	rbase,	___,	lit,	___) \
	_(FUNCC,	rbase,	___,	___,	___) \
	_(FUNCCW,	rbase,	___,	___,	___) \
	\
	/* specific purpose bc. */	\
	_(VARGN,	dst, ___,	lit,	___) \
	_(VARGSTR,	dst, ___,	lit,	___) \
	_(VPROBENAME,	dst, ___,	lit,	___) \
	_(VPID,		dst, ___,	lit,	___) \
	_(VTID,		dst, ___,	lit,	___) \
	_(VUID,		dst, ___,	lit,	___) \
	_(VCPU,		dst, ___,	lit,	___) \
	_(VEXECNAME,	dst, ___,	lit,	___) \
	\
	_(GFUNC,	dst, ___,	___,	___) /*load global C function*/

/* Bytecode opcode numbers. */
typedef enum {
#define BCENUM(name, ma, mb, mc, mt)	BC_##name,
	BCDEF(BCENUM)
#undef BCENUM
	BC__MAX
} BCOp;

KP_STATIC_ASSERT((int)BC_ISEQV+1 == (int)BC_ISNEV);
KP_STATIC_ASSERT(((int)BC_ISEQV^1) == (int)BC_ISNEV);
KP_STATIC_ASSERT(((int)BC_ISEQS^1) == (int)BC_ISNES);
KP_STATIC_ASSERT(((int)BC_ISEQN^1) == (int)BC_ISNEN);
KP_STATIC_ASSERT(((int)BC_ISEQP^1) == (int)BC_ISNEP);
KP_STATIC_ASSERT(((int)BC_ISLT^1) == (int)BC_ISGE);
KP_STATIC_ASSERT(((int)BC_ISLE^1) == (int)BC_ISGT);
KP_STATIC_ASSERT(((int)BC_ISLT^3) == (int)BC_ISGT);
KP_STATIC_ASSERT((int)BC_IST-(int)BC_ISTC == (int)BC_ISF-(int)BC_ISFC);
KP_STATIC_ASSERT((int)BC_CALLT-(int)BC_CALL == (int)BC_CALLMT-(int)BC_CALLM);
KP_STATIC_ASSERT((int)BC_CALLMT + 1 == (int)BC_CALLT);
KP_STATIC_ASSERT((int)BC_RETM + 1 == (int)BC_RET);
KP_STATIC_ASSERT((int)BC_FORL + 1 == (int)BC_IFORL);
KP_STATIC_ASSERT((int)BC_FORL + 2 == (int)BC_JFORL);
KP_STATIC_ASSERT((int)BC_ITERL + 1 == (int)BC_IITERL);
KP_STATIC_ASSERT((int)BC_ITERL + 2 == (int)BC_JITERL);
KP_STATIC_ASSERT((int)BC_LOOP + 1 == (int)BC_ILOOP);
KP_STATIC_ASSERT((int)BC_LOOP + 2 == (int)BC_JLOOP);
KP_STATIC_ASSERT((int)BC_FUNCF + 1 == (int)BC_IFUNCF);
KP_STATIC_ASSERT((int)BC_FUNCF + 2 == (int)BC_JFUNCF);
KP_STATIC_ASSERT((int)BC_FUNCV + 1 == (int)BC_IFUNCV);
KP_STATIC_ASSERT((int)BC_FUNCV + 2 == (int)BC_JFUNCV);

/* This solves a circular dependency problem, change as needed. */
#define FF_next_N	4

/* Stack slots used by FORI/FORL, relative to operand A. */
enum {
	FORL_IDX, FORL_STOP, FORL_STEP, FORL_EXT
};

/* Bytecode operand modes. ORDER BCMode */
typedef enum {
	/* Mode A must be <= 7 */
	BCMnone, BCMdst, BCMbase, BCMvar, BCMrbase, BCMuv,
	BCMlit, BCMlits, BCMpri, BCMnum, BCMstr, BCMtab, BCMfunc,
	BCMjump, BCMcdata,
	BCM_max
} BCMode;

#define BCM___	BCMnone

#define bcmode_a(op)	((BCMode)(bc_mode[op] & 7))
#define bcmode_b(op)	((BCMode)((bc_mode[op] >> 3) & 15))
#define bcmode_c(op)	((BCMode)((bc_mode[op] >> 7) & 15))
#define bcmode_d(op)	bcmode_c(op)
#define bcmode_hasd(op)	((bc_mode[op] & (15 << 3)) == (BCMnone << 3))
#define bcmode_mm(op)	((MMS)(bc_mode[op] >> 11))

#define BCMODE(name, ma, mb, mc, mm) \
	(BCM##ma | (BCM##mb << 3) | (BCM##mc << 7)|(MM_##mm << 11)),
#define BCMODE_FF	0

static inline int bc_isret(BCOp op)
{
	return (op == BC_RETM || op == BC_RET || op == BC_RET0 ||
		op == BC_RET1);
}

/* 
 * Metamethod definition
 * Note ktap don't use any lua methmethod currently.
 */
typedef enum {
	MM_lt,
	MM_le,
	MM_eq,
	MM_unm,
	MM_add,
	MM_sub,
	MM_mul,
	MM_div,
	MM_mod,
	MM_pow,
	MM_concat,
	MM_gc,
	MM_index,
	MM_newindex,
	MM_call,
	MM__MAX,
	MM____ = MM__MAX
} MMS;


/* -- Bytecode dump format ------------------------------------------------ */

/*
** dump   = header proto+ 0U
** header = ESC 'L' 'J' versionB flagsU [namelenU nameB*]
** proto  = lengthU pdata
** pdata  = phead bcinsW* uvdataH* kgc* knum* [debugB*]
** phead  = flagsB numparamsB framesizeB numuvB numkgcU numknU numbcU
**          [debuglenU [firstlineU numlineU]]
** kgc    = kgctypeU { ktab | (loU hiU) | (rloU rhiU iloU ihiU) | strB* }
** knum   = intU0 | (loU1 hiU)
** ktab   = narrayU nhashU karray* khash*
** karray = ktabk
** khash  = ktabk ktabk
** ktabk  = ktabtypeU { intU | (loU hiU) | strB* }
**
** B = 8 bit, H = 16 bit, W = 32 bit, U = ULEB128 of W, U0/U1 = ULEB128 of W+1
*/

/* Bytecode dump header. */
#define BCDUMP_HEAD1		0x15
#define BCDUMP_HEAD2		0x22
#define BCDUMP_HEAD3		0x06

/* If you perform *any* kind of private modifications to the bytecode itself
** or to the dump format, you *must* set BCDUMP_VERSION to 0x80 or higher.
*/
#define BCDUMP_VERSION		1

/* Compatibility flags. */
#define BCDUMP_F_BE		0x01
#define BCDUMP_F_STRIP		0x02
#define BCDUMP_F_FFI		0x04

#define BCDUMP_F_KNOWN		(BCDUMP_F_FFI*2-1)

/* Type codes for the GC constants of a prototype. Plus length for strings. */
enum {
	BCDUMP_KGC_CHILD, BCDUMP_KGC_TAB, BCDUMP_KGC_I64, BCDUMP_KGC_U64,
	BCDUMP_KGC_COMPLEX, BCDUMP_KGC_STR
};

/* Type codes for the keys/values of a constant table. */
enum {
	BCDUMP_KTAB_NIL, BCDUMP_KTAB_FALSE, BCDUMP_KTAB_TRUE,
	BCDUMP_KTAB_INT, BCDUMP_KTAB_NUM, BCDUMP_KTAB_STR
};

#endif /* __KTAP_BC_H__ */
