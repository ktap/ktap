/*
 * strfmt.c - printf implementation
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

#include <linux/ctype.h>
#include "../include/ktap.h"

/* get argument operation macro */
#define GetArg(ks, n)	((ks)->ci->func + (n))
#define GetArgN(ks)	((int)(ks->top - (ks->ci->func + 1)))

/* macro to `unsign' a character */
#define uchar(c)	((unsigned char)(c))

#define L_ESC		'%'

/* valid flags in a format specification */
#define FLAGS	"-+ #0"

#define INTFRMLEN	"ll"
#define INTFRM_T	long long

/*
 * maximum size of each format specification (such as '%-099.99d')
 * (+10 accounts for %99.99x plus margin of error)
 */
#define MAX_FORMAT	(sizeof(FLAGS) + sizeof(INTFRMLEN) + 10)

static const char *scanformat(ktap_State *ks, const char *strfrmt, char *form)
{
	const char *p = strfrmt;
	while (*p != '\0' && strchr(FLAGS, *p) != NULL)
		p++;  /* skip flags */

	if ((size_t)(p - strfrmt) >= sizeof(FLAGS)/sizeof(char))
		kp_runerror(ks, "invalid format (repeated flags)");

	if (isdigit(uchar(*p)))
		p++;  /* skip width */

	if (isdigit(uchar(*p)))
		p++;  /* (2 digits at most) */

	if (*p == '.') {
		p++;
		if (isdigit(uchar(*p)))
			p++;  /* skip precision */
		if (isdigit(uchar(*p)))
			p++;  /* (2 digits at most) */
	}

	if (isdigit(uchar(*p)))
		kp_runerror(ks, "invalid format (width or precision too long)");

	*(form++) = '%';
	memcpy(form, strfrmt, (p - strfrmt + 1) * sizeof(char));
	form += p - strfrmt + 1;
	*form = '\0';
	return p;
}


/*
 * add length modifier into formats
 */
static void addlenmod(char *form, const char *lenmod)
{
	size_t l = strlen(form);
	size_t lm = strlen(lenmod);
	char spec = form[l - 1];

	strcpy(form + l - 1, lenmod);
	form[l + lm - 1] = spec;
	form[l + lm] = '\0';
}


static void ktap_argerror(ktap_State *ks, int narg, const char *extramsg)
{
	kp_runerror(ks, "bad argument #%d: (%s)", narg, extramsg);
}


#define ktap_argcheck(ks, cond, numarg, extramsg)  \
		do { \
			if (cond) \
				ktap_argerror(ks, (numarg), (extramsg)); \
		} while (0)

int kp_strfmt(ktap_State *ks, struct trace_seq *seq)
{
	int arg = 1;
	size_t sfl;
	Tvalue *arg_fmt = GetArg(ks, 1);
	int argnum = GetArgN(ks);
	const char *strfrmt, *strfrmt_end;

	strfrmt = svalue(arg_fmt);
	sfl = rawtsvalue(arg_fmt)->tsv.len;
	strfrmt_end = strfrmt + sfl;

	while (strfrmt < strfrmt_end) {
		if (*strfrmt != L_ESC)
			trace_seq_putc(seq, *strfrmt++);
		else if (*++strfrmt == L_ESC)
			trace_seq_putc(seq, *strfrmt++);
		else { /* format item */
			char form[MAX_FORMAT];  /* to store the format (`%...') */

			if (++arg > argnum)
				ktap_argerror(ks, arg, "no value");

			strfrmt = scanformat(ks, strfrmt, form);
			switch (*strfrmt++) {
			case 'c':
				trace_seq_printf(seq, form, nvalue(GetArg(ks, arg)));
				break;
			case 'd':  case 'i': {
				ktap_Number n = nvalue(GetArg(ks, arg));
				INTFRM_T ni = (INTFRM_T)n;
				#if 0
				INTFRM_T ni = (INTFRM_T)n;
				ktap_Number diff = n - (ktap_Number)ni;
				ktap_argcheck(ks, -1 < diff && diff < 1, arg,
					"not a number in proper range");
				#endif
				addlenmod(form, INTFRMLEN);
				trace_seq_printf(seq, form, ni);
				break;
			}
			case 'o':  case 'u':  case 'x':  case 'X': {
				ktap_Number n = nvalue(GetArg(ks, arg));
				unsigned INTFRM_T ni = (unsigned INTFRM_T)n;
				addlenmod(form, INTFRMLEN);
				trace_seq_printf(seq, form, ni);
				break;
			}
			case 's': {
				const char *s = svalue(GetArg(ks, arg));
				size_t l = rawtsvalue((GetArg(ks, arg)))->tsv.len;
				if (!strchr(form, '.') && l >= 100) {
					/*
					 * no precision and string is too long to be formatted;
					 * keep original string
					 */
					trace_seq_printf(seq, "%s", s);
					break;
				} else {
					trace_seq_printf(seq, form, s);
					break;
				}
			}
			default: /* also treat cases `pnLlh' */
				kp_runerror(ks, "invalid option " KTAP_QL("%%%c") " to "
					KTAP_QL("format"), *(strfrmt - 1));
			}
		}
	}

	return 0;
}

