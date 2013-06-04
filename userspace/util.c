/*
 * util.c
 *
 * Author: Jovi Zhang <bookjovi@gmail.com>
 *         zhangwei(Jovi) <jovi.zhangwei@huawei.com>
 *
 * Code is copied from lua initially.
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/ktap_types.h"
#include "ktapc.h"


/*
** converts an integer to a "floating point byte", represented as
** (eeeeexxx), where the real value is (1xxx) * 2^(eeeee - 1) if
** eeeee != 0 and (xxx) otherwise.
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


ktap_Number ktapc_arith(int op, ktap_Number v1, ktap_Number v2)
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


static int isneg(const char **s)
{
	if (**s == '-') {
		(*s)++;
		return 1;
	} else if (**s == '+')
		(*s)++;

	return 0;
}


static ktap_Number readhexa(const char **s, ktap_Number r, int *count)
{
	for (; isxdigit((unsigned char)(**s)); (*s)++) {  /* read integer part */
		r = (r * 16.0) + (ktap_Number)(ktapc_hexavalue((unsigned char)(**s)));
		(*count)++;
	}

	return r;
}


/*
** convert an hexadecimal numeric string to a number, following
** C99 specification for 'strtod'
*/
static ktap_Number strx2number(const char *s, char **endptr)
{
	ktap_Number r = 0.0;
	int e = 0, i = 0;
	int neg = 0;  /* 1 if number is negative */

	*endptr = (char *)s;  /* nothing is valid yet */
	while (isspace((unsigned char)(*s)))
		s++;  /* skip initial spaces */

	neg = isneg(&s);  /* check signal */
	if (!(*s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')))  /* check '0x' */
		return 0.0;  /* invalid format (no '0x') */

	s += 2;  /* skip '0x' */
	r = readhexa(&s, r, &i);  /* read integer part */
	if (*s == '.') {
		s++;  /* skip dot */
		r = readhexa(&s, r, &e);  /* read fractional part */
	}

	if (i == 0 && e == 0)
		return 0.0;  /* invalid format (no digit) */
	e *= -4;  /* each fractional digit divides value by 2^-4 */
	*endptr = (char *)s;  /* valid up to here */

	if (*s == 'p' || *s == 'P') {  /* exponent part? */
		int exp1 = 0;
		int neg1;

		s++;  /* skip 'p' */
		neg1 = isneg(&s);  /* signal */
		if (!isdigit((unsigned char)(*s)))
			goto ret;  /* must have at least one digit */
		while (isdigit((unsigned char)(*s)))  /* read exponent */
			exp1 = exp1 * 10 + *(s++) - '0';
		if (neg1) exp1 = -exp1;
			e += exp1;
	}

	*endptr = (char *)s;  /* valid up to here */
 ret:
	if (neg)
		r = -r;

	return ldexp(r, e);
}


int ktapc_str2d(const char *s, size_t len, ktap_Number *result)
{
	char *endptr;

	if (strpbrk(s, "nN"))  /* reject 'inf' and 'nan' */
		return 0;
	else if (strpbrk(s, "xX"))  /* hexa? */
		*result = strx2number(s, &endptr);
	else
		*result = strtod(s, &endptr);

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

