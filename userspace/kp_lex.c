/*
 * Lexical analyzer.
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
#include "../include/ktap_err.h"
#include "kp_util.h"
#include "kp_lex.h"
#include "kp_parse.h"

/* lexer token names. */
static const char *const tokennames[] = {
#define TKSTR1(name)		#name,
#define TKSTR2(name, sym)	#sym,
TKDEF(TKSTR1, TKSTR2)
#undef TKSTR1
#undef TKSTR2
  NULL
};

/* -- Buffer handling ----------------------------------------------------- */

#define LEX_EOF			(-1)
#define lex_iseol(ls)		(ls->c == '\n' || ls->c == '\r')

/* Get next character. */
static inline LexChar lex_next(LexState *ls)
{
	return (ls->c = ls->p < ls->pe ? (LexChar)(uint8_t)*ls->p++ : LEX_EOF);
}

/* Save character. */
static inline void lex_save(LexState *ls, LexChar c)
{
	kp_buf_putb(&ls->sb, c);
}

/* Save previous character and get next character. */
static inline LexChar lex_savenext(LexState *ls)
{
	lex_save(ls, ls->c);
	return lex_next(ls);
}

/* Skip line break. Handles "\n", "\r", "\r\n" or "\n\r". */
static void lex_newline(LexState *ls)
{
	LexChar old = ls->c;

	kp_assert(lex_iseol(ls));
	lex_next(ls);  /* Skip "\n" or "\r". */
	if (lex_iseol(ls) && ls->c != old)
		lex_next(ls);  /* Skip "\n\r" or "\r\n". */
	if (++ls->linenumber >= KP_MAX_LINE)
		kp_lex_error(ls, ls->tok, KP_ERR_XLINES);
}

/* -- Scanner for terminals ----------------------------------------------- */

static int kp_str2d(const char *s, size_t len, ktap_number *result)
{
	char *endptr;

	if (strpbrk(s, "nN"))  /* reject 'inf' and 'nan' */
		return 0;
	else
		*result = (long)strtoul(s, &endptr, 0);

	if (endptr == s)
		return 0;  /* nothing recognized */
	while (kp_char_isspace((unsigned char)(*endptr)))
		endptr++;
	return (endptr == s + len);  /* OK if no trailing characters */
}


/* Parse a number literal. */
static void lex_number(LexState *ls, ktap_val_t *tv)
{
	LexChar c, xp = 'e';
	ktap_number n = 0;

	kp_assert(kp_char_isdigit(ls->c));
	if ((c = ls->c) == '0' && (lex_savenext(ls) | 0x20) == 'x')
		xp = 'p';
	while (kp_char_isident(ls->c) || ls->c == '.' ||
		((ls->c == '-' || ls->c == '+') && (c | 0x20) == xp)) {
		c = ls->c;
		lex_savenext(ls);
	}
	lex_save(ls, '\0');
	if (!kp_str2d(sbufB(&ls->sb), sbuflen(&ls->sb) - 1, &n))
			kp_lex_error(ls, ls->tok, KP_ERR_XNUMBER);
	set_number(tv, n);
}

/* Skip equal signs for "[=...=[" and "]=...=]" and return their count. */
static int lex_skipeq(LexState *ls)
{
	int count = 0;
	LexChar s = ls->c;

	kp_assert(s == '[' || s == ']');
	while (lex_savenext(ls) == '=')
		count++;
	return (ls->c == s) ? count : (-count) - 1;
}

/* Parse a long string or long comment (tv set to NULL). */
static void lex_longstring(LexState *ls, ktap_val_t *tv, int sep)
{
	lex_savenext(ls);  /* Skip second '['. */
	if (lex_iseol(ls))  /* Skip initial newline. */
		lex_newline(ls);
	for (;;) {
		switch (ls->c) {
		case LEX_EOF:
			kp_lex_error(ls, TK_eof,
					tv ? KP_ERR_XLSTR : KP_ERR_XLCOM);
			break;
		case ']':
			if (lex_skipeq(ls) == sep) {
				lex_savenext(ls);  /* Skip second ']'. */
				goto endloop;
			}
			break;
		case '\n':
		case '\r':
			lex_save(ls, '\n');
			lex_newline(ls);
			if (!tv) /* Don't waste space for comments. */
				kp_buf_reset(&ls->sb);
			break;
		default:
			lex_savenext(ls);
			break;
		}
	}
 endloop:
	if (tv) {
		ktap_str_t *str = kp_parse_keepstr(ls,
					sbufB(&ls->sb) + (2 + (int)sep),
					sbuflen(&ls->sb) - 2*(2 + (int)sep));
		set_string(tv, str);
	}
}

/* Parse a string. */
static void lex_string(LexState *ls, ktap_val_t *tv)
{
	LexChar delim = ls->c;  /* Delimiter is '\'' or '"'. */

	lex_savenext(ls);
	while (ls->c != delim) {
		switch (ls->c) {
		case LEX_EOF:
			kp_lex_error(ls, TK_eof, KP_ERR_XSTR);
			continue;
		case '\n':
		case '\r':
			kp_lex_error(ls, TK_string, KP_ERR_XSTR);
			continue;
		case '\\': {
			LexChar c = lex_next(ls);  /* Skip the '\\'. */
			switch (c) {
			case 'a': c = '\a'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'v': c = '\v'; break;
			case 'x':  /* Hexadecimal escape '\xXX'. */
				c = (lex_next(ls) & 15u) << 4;
				if (!kp_char_isdigit(ls->c)) {
					if (!kp_char_isxdigit(ls->c))
						goto err_xesc;
					c += 9 << 4;
				}
				c += (lex_next(ls) & 15u);
				if (!kp_char_isdigit(ls->c)) {
					if (!kp_char_isxdigit(ls->c))
						goto err_xesc;
					c += 9;
				}
				break;
			case 'z':  /* Skip whitespace. */
				lex_next(ls);
				while (kp_char_isspace(ls->c))
					if (lex_iseol(ls))
						lex_newline(ls);
					else
						lex_next(ls);
					continue;
			case '\n': case '\r':
				lex_save(ls, '\n');
				lex_newline(ls);
				continue;
			case '\\': case '\"': case '\'':
				break;
			case LEX_EOF:
				continue;
			default:
				if (!kp_char_isdigit(c))
					goto err_xesc;
				c -= '0';  /* Decimal escape '\ddd'. */
				if (kp_char_isdigit(lex_next(ls))) {
					c = c*10 + (ls->c - '0');
					if (kp_char_isdigit(lex_next(ls))) {
						c = c*10 + (ls->c - '0');
						if (c > 255) {
 err_xesc:
							kp_lex_error(ls,
								TK_string,
								KP_ERR_XESC);
						}
						lex_next(ls);
					}
				}
				lex_save(ls, c);
				continue;
			}
			lex_save(ls, c);
			lex_next(ls);
			continue;
		}
		default:
			lex_savenext(ls);
			break;
		}
	}
	lex_savenext(ls);  /* Skip trailing delimiter. */
	set_string(tv,
		kp_parse_keepstr(ls, sbufB(&ls->sb)+1, sbuflen(&ls->sb)-2));
}

/* lex helper for parse_trace and parse_timer */
void kp_lex_read_string_until(LexState *ls, int c)
{
	ktap_str_t *ts;

	kp_buf_reset(&ls->sb);

	while (ls->c == ' ')
		lex_next(ls);

	do {
		lex_savenext(ls);
	} while (ls->c != c && ls->c != LEX_EOF);

	if (ls->c != c)
		kp_lex_error(ls, ls->tok, KP_ERR_XTOKEN, c);

	ts = kp_parse_keepstr(ls, sbufB(&ls->sb), sbuflen(&ls->sb));
	ls->tok = TK_string;
	set_string(&ls->tokval, ts);
}


/* -- Main lexical scanner ------------------------------------------------ */

/* Get next lexical token. */
static LexToken lex_scan(LexState *ls, ktap_val_t *tv)
{
	kp_buf_reset(&ls->sb);
	for (;;) {
		if (kp_char_isident(ls->c)) {
			ktap_str_t *s;
			if (kp_char_isdigit(ls->c)) {  /* Numeric literal. */
				lex_number(ls, tv);
				return TK_number;
			}
			/* Identifier or reserved word. */
			do {
				lex_savenext(ls);
			} while (kp_char_isident(ls->c));
			s = kp_parse_keepstr(ls, sbufB(&ls->sb),
						sbuflen(&ls->sb));
			set_string(tv, s);
			if (s->reserved > 0)  /* Reserved word? */
				return TK_OFS + s->reserved;
			return TK_name;
		}

		switch (ls->c) {
		case '\n':
		case '\r':
			lex_newline(ls);
			continue;
		case ' ':
		case '\t':
		case '\v':
		case '\f':
			lex_next(ls);
			continue;

		case '#':
			while (!lex_iseol(ls) && ls->c != LEX_EOF)
				lex_next(ls);
			break;
		case '-':
			lex_next(ls);
			if (ls->c != '-')
				return '-';
			lex_next(ls);
			if (ls->c == '[') { /* Long comment "--[=*[...]=*]". */
				int sep = lex_skipeq(ls);
				/* `lex_skipeq' may dirty the buffer */
				kp_buf_reset(&ls->sb);
				if (sep >= 0) {
					lex_longstring(ls, NULL, sep);
					kp_buf_reset(&ls->sb);
					continue;
				}
			}
			/* Short comment "--.*\n". */
			while (!lex_iseol(ls) && ls->c != LEX_EOF)
				lex_next(ls);
			continue;
		case '[': {
			int sep = lex_skipeq(ls);
			if (sep >= 0) {
				lex_longstring(ls, tv, sep);
				return TK_string;
			} else if (sep == -1) {
				return '[';
			} else {
				kp_lex_error(ls, TK_string, KP_ERR_XLDELIM);
				continue;
			}
		}
		case '+': {
			lex_next(ls);
			if (ls->c != '=')
				return '+';
			else {
				lex_next(ls);
				return TK_incr;
			}
		}
		case '=':
			lex_next(ls);
			if (ls->c != '=')
				return '=';
			else {
				lex_next(ls);
				return TK_eq;
			}
		case '<':
			lex_next(ls);
			if (ls->c != '=')
				return '<';
			else {
				lex_next(ls);
				return TK_le;
			}
		case '>':
			lex_next(ls);
			if (ls->c != '=')
				return '>';
			else {
				lex_next(ls);
				return TK_ge;
			}
		case '!':
      			lex_next(ls);
			if (ls->c != '=')
				return TK_not;
			else {
				lex_next(ls);
				return TK_ne;
			}
		case ':':
			lex_next(ls);
			if (ls->c != ':')
				return ':';
			else {
				lex_next(ls);
				return TK_label;
			}
		case '"':
		case '\'':
			lex_string(ls, tv);
			return TK_string;
		case '.':
			if (lex_savenext(ls) == '.') {
				lex_next(ls);
				if (ls->c == '.') {
					lex_next(ls);
					return TK_dots;   /* ... */
				}
				return TK_concat;   /* .. */
			} else if (!kp_char_isdigit(ls->c)) {
				return '.';
			} else {
				lex_number(ls, tv);
				return TK_number;
			}
		case LEX_EOF:
			return TK_eof;
		case '&':
			lex_next(ls);
			if (ls->c != '&')
				return '&';
			else {
				lex_next(ls);
				return TK_and;
			}
		case '|':
			lex_next(ls);
			if (ls->c != '|')
				return '|';
			else {
				lex_next(ls);
				return TK_or;
			}
		default: {
			LexChar c = ls->c;
			lex_next(ls);
			return c;  /* Single-char tokens (+ - / ...). */
		}
		}
	}
}

/* -- Lexer API ----------------------------------------------------------- */

/* Setup lexer state. */
int kp_lex_setup(LexState *ls, const char *str)
{
	ls->fs = NULL;
	ls->pe = ls->p = NULL;
	ls->p = str;
	ls->pe = str + strlen(str);
	ls->vstack = NULL;
	ls->sizevstack = 0;
	ls->vtop = 0;
	ls->bcstack = NULL;
	ls->sizebcstack = 0;
	ls->lookahead = TK_eof;  /* No look-ahead token. */
	ls->linenumber = 1;
	ls->lastline = 1;
	lex_next(ls);  /* Read-ahead first char. */
	if (ls->c == 0xef && ls->p + 2 <= ls->pe &&
		(uint8_t)ls->p[0] == 0xbb &&
		(uint8_t)ls->p[1] == 0xbf) {/* Skip UTF-8 BOM (if buffered). */
		ls->p += 2;
		lex_next(ls);
	}
	if (ls->c == '#') {  /* Skip POSIX #! header line. */
		do {
			lex_next(ls);
			if (ls->c == LEX_EOF)
				return 0;
		} while (!lex_iseol(ls));
		lex_newline(ls);
	}
	return 0;
}

/* Cleanup lexer state. */
void kp_lex_cleanup(LexState *ls)
{
	free(ls->bcstack);
	free(ls->vstack);
	kp_buf_free(&ls->sb);
}

/* Return next lexical token. */
void kp_lex_next(LexState *ls)
{
	ls->lastline = ls->linenumber;
	if (ls->lookahead == TK_eof) {  /* No lookahead token? */
		ls->tok = lex_scan(ls, &ls->tokval);  /* Get next token. */
	} else {  /* Otherwise return lookahead token. */
		ls->tok = ls->lookahead;
		ls->lookahead = TK_eof;
		ls->tokval = ls->lookaheadval;
	}
}

/* Look ahead for the next token. */
LexToken kp_lex_lookahead(LexState *ls)
{
	kp_assert(ls->lookahead == TK_eof);
	ls->lookahead = lex_scan(ls, &ls->lookaheadval);
	return ls->lookahead;
}

/* Convert token to string. */
const char *kp_lex_token2str(LexState *ls, LexToken tok)
{
	if (tok > TK_OFS)
		return tokennames[tok-TK_OFS-1];
	else if (!kp_char_iscntrl(tok))
		return kp_sprintf("%c", tok);
	else
		return kp_sprintf("char(%d)", tok);
}

/* Lexer error. */
void kp_lex_error(LexState *ls, LexToken tok, ErrMsg em, ...)
{
	const char *tokstr;
	va_list argp;

	if (tok == 0) {
		tokstr = NULL;
	} else if (tok == TK_name || tok == TK_string || tok == TK_number) {
		lex_save(ls, '\0');
		tokstr = sbufB(&ls->sb);
	} else {
		tokstr = kp_lex_token2str(ls, tok);
	}

	va_start(argp, em);
	kp_err_lex(ls->chunkname, tokstr, ls->linenumber, em, argp);
	va_end(argp);
}

/* Initialize strings for reserved words. */
void kp_lex_init()
{
	uint32_t i;

	for (i = 0; i < TK_RESERVED; i++) {
		ktap_str_t *s = kp_str_newz(tokennames[i]);
		s->reserved = (uint8_t)(i+1);
	}
}

