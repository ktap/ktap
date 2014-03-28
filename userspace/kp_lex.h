/*
 * Lexical analyzer.
 *
 * Copyright (C) 2012-2014 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Adapted from luajit and lua interpreter.
 * Copyright (C) 2005-2014 Mike Pall.
 * Copyright (C) 1994-2008 Lua.org, PUC-Rio.
 */

#ifndef _KTAP_LEX_H
#define _KTAP_LEX_H

#include <stdarg.h>
#include "../include/err.h"
#include "../include/ktap_bc.h"
#include "kp_util.h"

/* ktap lexer tokens. */
#define TKDEF(_, __) \
	_(trace) _(trace_end) _(argstr) _(probename) _(ffi) \
	_(arg0)_(arg1) _(arg2) _(arg3) _(arg4) _(arg5) _(arg6) _(arg7) \
	_(arg8) _(arg9) _(profile) _(tick) \
	_(pid) _(tid) _(uid) _(cpu) _(execname) __(incr, +=) \
	__(and, &&) _(break) _(do) _(else) _(elseif) _(end) _(false) \
	_(for) _(function) _(goto) _(if) _(in) __(local, var) _(nil) \
	__(not, !) __(or, ||) \
	_(repeat) _(return) _(then) _(true) _(until) _(while) \
	__(concat, ..) __(dots, ...) __(eq, ==) __(ge, >=) __(le, <=) \
	__(ne, !=) __(label, ::) __(number, <number>) __(name, <name>) \
	__(string, <string>) __(eof, <eof>)

enum {
	TK_OFS = 256,
#define TKENUM1(name)		TK_##name,
#define TKENUM2(name, sym)	TK_##name,
	TKDEF(TKENUM1, TKENUM2)
#undef TKENUM1
#undef TKENUM2
	TK_RESERVED = TK_while - TK_OFS
};

typedef int LexChar;	/* Lexical character. Unsigned ext. from char. */
typedef int LexToken;	/* Lexical token. */

/* Combined bytecode ins/line. Only used during bytecode generation. */
typedef struct BCInsLine {
	BCIns ins;		/* Bytecode instruction. */
	BCLine line;		/* Line number for this bytecode. */
} BCInsLine;

/* Info for local variables. Only used during bytecode generation. */
typedef struct VarInfo {
	ktap_str_t *name;	/* Local variable name or goto/label name. */
	BCPos startpc;	/* First point where the local variable is active. */
	BCPos endpc;	/* First point where the local variable is dead. */
	uint8_t slot;	/* Variable slot. */
	uint8_t info;	/* Variable/goto/label info. */
} VarInfo;

/* lexer state. */
typedef struct LexState {
	struct FuncState *fs;	/* Current FuncState. Defined in kp_parse.c. */
	ktap_val_t tokval;	/* Current token value. */
	ktap_val_t lookaheadval;/* Lookahead token value. */
	const char *p;	/* Current position in input buffer. */
	const char *pe;	/* End of input buffer. */
	LexChar c;		/* Current character. */
	LexToken tok;		/* Current token. */
	LexToken lookahead;	/* Lookahead token. */
	SBuf sb;		/* String buffer for tokens. */
	BCLine linenumber;	/* Input line counter. */
	BCLine lastline;	/* Line of last token. */
	ktap_str_t *chunkname;/* Current chunk name (interned string). */
	const char *chunkarg;	/* Chunk name argument. */
	const char *mode;/* Allow loading bytecode (b) and/or source text (t) */
	VarInfo *vstack;/* Stack for names and extents of local variables. */
	int sizevstack;	/* Size of variable stack. */
	int vtop;	/* Top of variable stack. */
	BCInsLine *bcstack;/* Stack for bytecode instructions/line numbers. */
	int sizebcstack;/* Size of bytecode stack. */
	uint32_t level;	/* Syntactical nesting level. */
} LexState;

int kp_lex_setup(LexState *ls, const char *str);
void kp_lex_cleanup(LexState *ls);
void kp_lex_next(LexState *ls);
void kp_lex_read_string_until(LexState *ls, int c);
LexToken kp_lex_lookahead(LexState *ls);
const char *kp_lex_token2str(LexState *ls, LexToken tok);
void kp_lex_error(LexState *ls, LexToken tok, ErrMsg em, ...);
void kp_lex_init(void);

#endif
