/*
 * util.c
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
#include "ktapc.h"

/*
 * converts an integer to a "floating point byte", represented as
 * (eeeeexxx), where the real value is (1xxx) * 2^(eeeee - 1) if
 * eeeee != 0 and (xxx) otherwise.
 */
int ktapc_int2fb(unsigned int x)
{
	int e = 0;  /* exponent */

	if (x < 8)
		return x;
	while (x >= 0x10) {
		x = (x+1) >> 1;
		e++;
	}
	return ((e+1) << 3) | ((int)x - 8);
}

/* converts back */
int ktapc_fb2int(int x)
{
	int e = (x >> 3) & 0x1f;

	if (e == 0)
		return x;
	else
		return ((x & 7) + 8) << (e - 1);
}

int ktapc_ceillog2(unsigned int x)
{
	static const u8 log_2[256] = {
	0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
	6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
	};
	int l = 0;

	x--;
	while (x >= 256) {
		l += 8;
		x >>= 8;
	}
	return l + log_2[x];
}

ktap_number ktapc_arith(int op, ktap_number v1, ktap_number v2)
{
	switch (op) {
	case KTAP_OPADD: return NUMADD(v1, v2);
	case KTAP_OPSUB: return NUMSUB(v1, v2);
	case KTAP_OPMUL: return NUMMUL(v1, v2);
	case KTAP_OPDIV: return NUMDIV(v1, v2);
	case KTAP_OPMOD: return NUMMOD(v1, v2);
	//case KTAP_OPPOW: return NUMPOW(v1, v2);
	case KTAP_OPUNM: return NUMUNM(v1);
	default: ktap_assert(0); return 0;
	}
}

int ktapc_hexavalue(int c)
{
	if (isdigit(c))
		return c - '0';
	else
		return tolower(c) - 'a' + 10;
}

int ktapc_str2d(const char *s, size_t len, ktap_number *result)
{
	char *endptr;

	if (strpbrk(s, "nN"))  /* reject 'inf' and 'nan' */
		return 0;
	else
		*result = (long)strtoul(s, &endptr, 0);

	if (endptr == s)
		return 0;  /* nothing recognized */
	while (isspace((unsigned char)(*endptr)))
		endptr++;
	return (endptr == s + len);  /* OK if no trailing characters */
}

/* number of chars of a literal string without the ending \0 */
#define LL(x)	(sizeof(x)/sizeof(char) - 1)

#define RETS	"..."
#define PRE	"[string \""
#define POS	"\"]"

#define addstr(a,b,l)	( memcpy(a,b,(l) * sizeof(char)), a += (l) )

void ktapc_chunkid(char *out, const char *source, size_t bufflen)
{
	size_t l = strlen(source);

	if (*source == '=') {  /* 'literal' source */
		if (l <= bufflen)  /* small enough? */
			memcpy(out, source + 1, l * sizeof(char));
		else {  /* truncate it */
			addstr(out, source + 1, bufflen - 1);
			*out = '\0';
		}
	} else if (*source == '@') {  /* file name */
		if (l <= bufflen)  /* small enough? */
			memcpy(out, source + 1, l * sizeof(char));
		else {  /* add '...' before rest of name */
			addstr(out, RETS, LL(RETS));
			bufflen -= LL(RETS);
			memcpy(out, source + 1 + l - bufflen, bufflen * sizeof(char));
		}
	} else {  /* string; format as [string "source"] */
		const char *nl = strchr(source, '\n');  /* find first new line (if any) */
		addstr(out, PRE, LL(PRE));  /* add prefix */
		bufflen -= LL(PRE RETS POS) + 1;  /* save space for prefix+suffix+'\0' */
		if (l < bufflen && nl == NULL) {  /* small one-line source? */
			addstr(out, source, l);  /* keep it */
		} else {
			if (nl != NULL)
				l = nl - source;  /* stop at first newline */
			if (l > bufflen)
				l = bufflen;
			addstr(out, source, l);
			addstr(out, RETS, LL(RETS));
		}
		memcpy(out, POS, (LL(POS) + 1) * sizeof(char));
	}
}


/*
 * strglobmatch is copyed from perf(linux/tools/perf/util/string.c)
 */

/* Character class matching */
static bool __match_charclass(const char *pat, char c, const char **npat)
{
	bool complement = false, ret = true;

	if (*pat == '!') {
		complement = true;
		pat++;
	}
	if (*pat++ == c)	/* First character is special */
		goto end;

	while (*pat && *pat != ']') {	/* Matching */
		if (*pat == '-' && *(pat + 1) != ']') {	/* Range */
			if (*(pat - 1) <= c && c <= *(pat + 1))
				goto end;
			if (*(pat - 1) > *(pat + 1))
				goto error;
			pat += 2;
		} else if (*pat++ == c)
			goto end;
	}
	if (!*pat)
		goto error;
	ret = false;

end:
	while (*pat && *pat != ']')	/* Searching closing */
		pat++;
	if (!*pat)
		goto error;
	*npat = pat + 1;
	return complement ? !ret : ret;

error:
	return false;
}

/* Glob/lazy pattern matching */
static bool __match_glob(const char *str, const char *pat, bool ignore_space)
{
	while (*str && *pat && *pat != '*') {
		if (ignore_space) {
			/* Ignore spaces for lazy matching */
			if (isspace(*str)) {
				str++;
				continue;
			}
			if (isspace(*pat)) {
				pat++;
				continue;
			}
		}
		if (*pat == '?') {	/* Matches any single character */
			str++;
			pat++;
			continue;
		} else if (*pat == '[')	/* Character classes/Ranges */
			if (__match_charclass(pat + 1, *str, &pat)) {
				str++;
				continue;
			} else
				return false;
		else if (*pat == '\\') /* Escaped char match as normal char */
			pat++;
		if (*str++ != *pat++)
			return false;
	}
	/* Check wild card */
	if (*pat == '*') {
		while (*pat == '*')
			pat++;
		if (!*pat)	/* Tail wild card matches all */
			return true;
		while (*str)
			if (__match_glob(str++, pat, ignore_space))
				return true;
	}
	return !*str && !*pat;
}

/**
 * strglobmatch - glob expression pattern matching
 * @str: the target string to match
 * @pat: the pattern string to match
 *
 * This returns true if the @str matches @pat. @pat can includes wildcards
 * ('*','?') and character classes ([CHARS], complementation and ranges are
 * also supported). Also, this supports escape character ('\') to use special
 * characters as normal character.
 *
 * Note: if @pat syntax is broken, this always returns false.
 */
bool strglobmatch(const char *str, const char *pat)
{
	return __match_glob(str, pat, false);
}

#define handle_error(str) do { perror(str); exit(-1); } while(0)

#define KALLSYMS_PATH "/proc/kallsyms"
/*
 * read kernel symbol from /proc/kallsyms
 */
unsigned long ktapc_read_ksym(ktap_string *ts)
{
	const char *symbol = getstr(ts);
	size_t ts_len = ts->tsv.len;
	unsigned long ret = 0;
	ssize_t size;
	FILE *file;
	char *line = NULL;
	size_t n;

	file = fopen(KALLSYMS_PATH, "r");
	if (file == NULL)
		handle_error("open " KALLSYMS_PATH " failed");

	while ((size = getline(&line, &n, file)) != -1) {
		char *sym_addr, *sym_name;

		sym_addr = strtok(line, " ");
		strtok(NULL, " ");
		sym_name = strtok(NULL, " ");
		if ((strncmp(symbol, sym_name, ts_len) == 0)
				&& (sym_name[ts_len] == '\n')) {
			ret = strtoul(sym_addr, NULL, 16);
			break;
		}
	}
	if (size < 0) {
		fprintf(stderr, "cannot read kernel symbol \"%s\" in %s\n",
			symbol, KALLSYMS_PATH);
		exit(EXIT_FAILURE);
	}

	fclose(file);
	free(line);

	if (ret == 0)
		handle_error("cannot find kernel symbol");

	return ret;
}

