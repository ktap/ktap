#include <stdarg.h>
#include "../cparser.h"

#define IS_CONST(tok) (IS_LITERAL(tok, "const") || IS_LITERAL(tok, "__const") \
			|| IS_LITERAL(tok, "__const__"))
#define IS_VOLATILE(tok) (IS_LITERAL(tok, "volatile") || \
				IS_LITERAL(tok, "__volatile") || \
				IS_LITERAL(tok, "__volatile__"))
#define IS_RESTRICT(tok) (IS_LITERAL(tok, "restrict") || \
				IS_LITERAL(tok, "__restrict") || \
				IS_LITERAL(tok, "__restrict__"))

#define max(a,b) ((a) < (b) ? (b) : (a))
#define min(a,b) ((a) < (b) ? (a) : (b))


enum etoken {
	/* 0 - 3 */
	TOK_NIL,
	TOK_NUMBER,
	TOK_STRING,
	TOK_TOKEN,

	/* the order of these values must match the token strings in lex.c */

	/* 4 - 5 */
	TOK_3_BEGIN,
	TOK_VA_ARG,

	/* 6 - 14 */
	TOK_2_BEGIN,
	TOK_LEFT_SHIFT, TOK_RIGHT_SHIFT, TOK_LOGICAL_AND, TOK_LOGICAL_OR,
	TOK_LESS_EQUAL, TOK_GREATER_EQUAL, TOK_EQUAL, TOK_NOT_EQUAL,

	/* 15 - 20 */
	TOK_1_BEGIN,
	TOK_OPEN_CURLY, TOK_CLOSE_CURLY, TOK_SEMICOLON, TOK_COMMA, TOK_COLON,
	/* 21 - 30 */
	TOK_ASSIGN, TOK_OPEN_PAREN, TOK_CLOSE_PAREN, TOK_OPEN_SQUARE, TOK_CLOSE_SQUARE,
	TOK_DOT, TOK_AMPERSAND, TOK_LOGICAL_NOT, TOK_BITWISE_NOT, TOK_MINUS,
	/* 31 - 40 */
	TOK_PLUS, TOK_STAR, TOK_DIVIDE, TOK_MODULUS, TOK_LESS,
	TOK_GREATER, TOK_BITWISE_XOR, TOK_BITWISE_OR, TOK_QUESTION, TOK_POUND,

	/* 41 - 43 */
	TOK_REFERENCE = TOK_AMPERSAND,
	TOK_MULTIPLY = TOK_STAR,
	TOK_BITWISE_AND = TOK_AMPERSAND,
};

struct token {
	enum etoken type;
	int64_t integer;
	const char *str;
	size_t size;
};

#define IS_LITERAL(TOK, STR) \
	(((TOK).size == sizeof(STR) - 1) && \
		0 == memcmp((TOK).str, STR, sizeof(STR) - 1))


static int parse_type_name(struct parser *P, char *type_name);
static void parse_argument(struct parser *P, struct cp_ctype *ct,
		struct token *pname, struct parser *asmname);
static int parse_attribute(struct parser *P, struct token *tok,
		struct cp_ctype *ct, struct parser *asmname);
static int parse_record(struct parser *P, struct cp_ctype *ct);
static void instantiate_typedef(struct parser *P, struct cp_ctype *tt,
		const struct cp_ctype *ft);


/* the order of tokens _must_ match the order of the enum etoken enum */

static char tok3[][4] = {
	"...", /* unused ">>=", "<<=", */
};

static char tok2[][3] = {
	"<<", ">>", "&&", "||", "<=",
	">=", "==", "!=",
	/* unused "+=", "-=", "*=", "/=", "%=", "&=", "^=",
	 * "|=", "++", "--", "->", "::", */
};

static char tok1[] = {
	'{', '}', ';', ',', ':',
	'=', '(', ')', '[', ']',
	'.', '&', '!', '~', '-',
	'+', '*', '/', '%', '<',
	'>', '^', '|', '?', '#'
};


/* this function never returns, but it's an idiom to use it in C functions
 * as return cp_error */
void cp_error(const char *err_msg_fmt, ...)
{
	va_list ap;

	fprintf(stderr, "cparser error:\n");

	va_start(ap, err_msg_fmt);
	vfprintf(stderr, err_msg_fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

static int set_struct_type_name(char *dst, const char *src, int len)
{
	int prefix_len = sizeof("struct ");

	if (len + prefix_len > MAX_TYPE_NAME_LEN)
		return -1;

	memset(dst, 0, MAX_TYPE_NAME_LEN);
	strcpy(dst, "struct ");
	strncat(dst, src, len);

	return 0;
}

static void increase_ptr_deref_level(struct parser *P, struct cp_ctype *ct)
{
	if (ct->pointers == POINTER_MAX) {
		cp_error("maximum number of pointer derefs reached - use a "
			"struct to break up the pointers on line %d", P->line);
	} else {
		ct->pointers++;
		ct->const_mask <<= 1;
	}
}

static int next_token(struct parser *P, struct token *tok)
{
	size_t i;
	const char *s = P->next;

	/* UTF8 BOM */
	if (s[0] == '\xEF' && s[1] == '\xBB' && s[2] == '\xBF') {
		s += 3;
	}

	/* consume whitespace and comments */
	for (;;) {
		/* consume whitespace */
		while (*s == '\t' || *s == '\n' || *s == ' '
				|| *s == '\v' || *s == '\r') {
			if (*s == '\n') {
				P->line++;
			}
			s++;
		}

		/* consume comments */
		if (*s == '/' && *(s+1) == '/') {

			s = strchr(s, '\n');
			if (!s) {
				cp_error("non-terminated comment");
			}

		} else if (*s == '/' && *(s+1) == '*') {
			s += 2;

			for (;;) {
				if (s[0] == '\0') {
					cp_error("non-terminated comment");
				} else if (s[0] == '*' && s[1] == '/') {
					s += 2;
					break;
				} else if (s[0] == '\n') {
					P->line++;
				}
				s++;
			}

		} else if (*s == '\0') {
			tok->type = TOK_NIL;
			return 0;

		} else {
			break;
		}
	}

	P->prev = s;

	for (i = 0; i < sizeof(tok3) / sizeof(tok3[0]); i++) {
		if (s[0] == tok3[i][0] && s[1] == tok3[i][1] && s[2] == tok3[i][2]) {
			tok->type = (enum etoken) (TOK_3_BEGIN + 1 + i);
			P->next = s + 3;
			goto end;
		}
	}

	for (i = 0; i < sizeof(tok2) / sizeof(tok2[0]); i++) {
		if (s[0] == tok2[i][0] && s[1] == tok2[i][1]) {
			tok->type = (enum etoken) (TOK_2_BEGIN + 1 + i);
			P->next = s + 2;
			goto end;
		}
	}

	for (i = 0; i < sizeof(tok1) / sizeof(tok1[0]); i++) {
		if (s[0] == tok1[i]) {
			tok->type = (enum etoken) (TOK_1_BEGIN + 1 + i);
			P->next = s + 1;
			goto end;
		}
	}

	if (*s == '.' || *s == '-' || ('0' <= *s && *s <= '9')) {
		/* number */
		tok->type = TOK_NUMBER;

		/* split out the negative case so we get the full range of
		 * bits for unsigned (eg to support 0xFFFFFFFF where
		 * sizeof(long) == 4 */
		if (*s == '-') {
			tok->integer = strtol(s, (char**) &s, 0);
		} else {
			tok->integer = strtoul(s, (char**) &s, 0);
		}

		while (*s == 'u' || *s == 'U' || *s == 'l' || *s == 'L') {
			s++;
		}

		P->next = s;
		goto end;

	} else if (*s == '\'' || *s == '\"') {
		/* "..." or '...' */
		char quote = *s;
		s++; /* jump over " */

		tok->type = TOK_STRING;
		tok->str = s;

		while (*s != quote) {
			if (*s == '\0' || (*s == '\\' && *(s+1) == '\0')) {
				cp_error("string not finished\n");
			}
			if (*s == '\\') {
				s++;
			}
			s++;
		}

		tok->size = s - tok->str;
		s++; /* jump over " */
		P->next = s;
		goto end;

	} else if (('a' <= *s && *s <= 'z') || ('A' <= *s && *s <= 'Z')
			|| *s == '_') {
		/* tokens */
		tok->type = TOK_TOKEN;
		tok->str = s;

		while (('a' <= *s && *s <= 'z') || ('A' <= *s && *s <= 'Z')
				|| *s == '_' || ('0' <= *s && *s <= '9')) {
			s++;
		}

		tok->size = s - tok->str;
		P->next = s;
		goto end;
	} else {
		cp_error("invalid character %d", P->line);
	}

end:
	return 1;
}

static void require_token(struct parser *P, struct token *tok)
{
	if (!next_token(P, tok)) {
		cp_error("unexpected end");
	}
}

static void check_token(struct parser *P, int type, const char *str,
				const char *err, ...)
{
	va_list ap;
	struct token tok;
	if (!next_token(P, &tok) || tok.type != type
			|| (tok.type == TOK_TOKEN && (tok.size != strlen(str)
				|| memcmp(tok.str, str, tok.size) != 0))) {

		va_start(ap, err);
		vfprintf(stderr, err, ap);
		va_end(ap);

		exit(EXIT_FAILURE);
	}
}

static void put_back(struct parser *P) {
	P->next = P->prev;
}

int64_t calculate_constant(struct parser *P);

/* parses out the base type of a type expression in a function declaration,
 * struct definition, typedef etc
 *
 * leaves the usr value of the type on the stack
 */
int parse_type(struct parser *P, struct cp_ctype *ct)
{
	struct token tok;

	memset(ct, 0, sizeof(*ct));

	require_token(P, &tok);

	/* get function attributes before the return type */
	while (parse_attribute(P, &tok, ct, NULL)) {
		require_token(P, &tok);
	}

	/* get const/volatile before the base type */
	for (;;) {
		if (tok.type != TOK_TOKEN) {
			cp_error("unexpected value before type name on line %d",
					P->line);
			return 0;
		} else if (IS_CONST(tok)) {
			ct->const_mask = 1;
			require_token(P, &tok);
		} else if (IS_VOLATILE(tok) || IS_RESTRICT(tok)) {
			/* ignored for now */
			require_token(P, &tok);
		} else {
			break;
		}
	}

	/* get base type */
	if (tok.type != TOK_TOKEN) {
		cp_error("unexpected value before type name on line %d", P->line);
		return 0;
	} else if (IS_LITERAL(tok, "struct")) {
		ct->type = STRUCT_TYPE;
		parse_record(P, ct);
	} else if (IS_LITERAL(tok, "union")) {
		ct->type = UNION_TYPE;
		parse_record(P, ct);
	} else if (IS_LITERAL(tok, "enum")) {
		ct->type = ENUM_TYPE;
		parse_record(P, ct);
	} else {
		put_back(P);

		/* lookup type */
		struct cp_ctype *lct;
		char cur_type_name[MAX_TYPE_NAME_LEN];

		memset(cur_type_name, 0, MAX_TYPE_NAME_LEN);
		parse_type_name(P, cur_type_name);
		lct = ctype_lookup_type(cur_type_name);
		if (!lct)
			cp_error("unknow type: \"%s\"\n", cur_type_name);

		instantiate_typedef(P, ct, lct);
	}

	while (next_token(P, &tok)) {
		if (tok.type != TOK_TOKEN) {
			put_back(P);
			break;
		} else if (IS_CONST(tok) || IS_VOLATILE(tok)) {
			/* ignore for now */
		} else {
			put_back(P);
			break;
		}
	}

	return 0;
}

enum test {TEST};

/* Parses an enum definition from after the open curly through to the close
 * curly. Expects the user table to be on the top of the stack
 */
static int parse_enum(struct parser *P, struct cp_ctype *type)
{
	struct token tok;
	int value = -1;

	/*@TODO clean up this function when enum support is added*/
	cp_error("TODO: enum not supported!\n");

	for (;;) {
		require_token(P, &tok);

		if (tok.type == TOK_CLOSE_CURLY) {
			break;
		} else if (tok.type != TOK_TOKEN) {
			cp_error("unexpected token in enum at line %d", P->line);
			return 0;
		}
		require_token(P, &tok);

		if (tok.type == TOK_COMMA || tok.type == TOK_CLOSE_CURLY) {
			/* we have an auto calculated enum value */
			value++;
		} else if (tok.type == TOK_ASSIGN) {
			/* we have an explicit enum value */
			value = (int) calculate_constant(P);
			require_token(P, &tok);
		} else {
			cp_error("unexpected token in enum at line %d", P->line);
			return 0;
		}

		if (tok.type == TOK_CLOSE_CURLY) {
			break;
		} else if (tok.type != TOK_COMMA) {
			cp_error("unexpected token in enum at line %d", P->line);
			return 0;
		}
	}

	type->base_size = sizeof(enum test);
	type->align_mask = sizeof(enum test) - 1;

	return 0;
}

/* Parses a struct from after the open curly through to the close curly. */
static int parse_struct(struct parser *P, const struct cp_ctype *ct)
{
	struct token tok;

	/* parse members */
	for (;;) {
		struct cp_ctype mbase;

		/* see if we're at the end of the struct */
		require_token(P, &tok);
		if (tok.type == TOK_CLOSE_CURLY) {
			break;
		} else if (ct->is_variable_struct) {
			cp_error("can't have members after a variable sized "
					"member on line %d", P->line);
			return -1;
		} else {
			put_back(P);
		}

		/* members are of the form
		 * <base type> <arg>, <arg>, <arg>;
		 * eg struct foo bar, *bar2[2];
		 * mbase is 'struct foo'
		 * mtype is '' then '*[2]'
		 * mname is 'bar' then 'bar2'
		 */

		parse_type(P, &mbase);

		for (;;) {
			struct token mname;
			struct cp_ctype mt = mbase;

			memset(&mname, 0, sizeof(mname));

			if (ct->is_variable_struct) {
				cp_error("can't have members after a variable "
					"sized member on line %d", P->line);
				return -1;
			}

			parse_argument(P, &mt, &mname, NULL);

			if (!mt.is_defined && (mt.pointers - mt.is_array) == 0) {
				cp_error("member type is undefined on line %d",
						P->line);
				return -1;
			}

			if (mt.type == VOID_TYPE
					&& (mt.pointers - mt.is_array) == 0) {
				cp_error("member type can not be void on line %d",
						P->line);
				return -1;
			}

			mt.has_member_name = (mname.size > 0);
			if (mt.has_member_name) {
				cp_push_ctype_with_name(&mt,
						mname.str, mname.size);
			} else {
				/* @TODO handle unnamed member (houqp) */
				cp_error("TODO: unnamed member not supported.");
				cp_push_ctype(&mt);
			}

			require_token(P, &tok);
			if (tok.type == TOK_SEMICOLON) {
				break;
			} else if (tok.type != TOK_COMMA) {
				cp_error("unexpected token in struct "
					"definition on line %d", P->line);
			}
		}
	}

	return 0;
}

/* copy over attributes that could be specified before the typedef eg
 * __attribute__(packed) const type_t */
static void instantiate_typedef(struct parser *P, struct cp_ctype *tt,
		const struct cp_ctype *ft)
{
	struct cp_ctype pt = *tt;
	*tt = *ft;

	tt->const_mask |= pt.const_mask;
	tt->is_packed = pt.is_packed;

	if (tt->is_packed) {
		tt->align_mask = 0;
	} else {
		/* Instantiate the typedef in the current packing. This may be
		 * further updated if a pointer is added or another alignment
		 * attribute is applied. If pt.align_mask is already non-zero
		 * than an increased alignment via __declspec(aligned(#)) has
		 * been set. */
		tt->align_mask = max(min(P->align_mask, tt->align_mask),
					pt.align_mask);
	}
}

/* this parses a struct or union starting with the optional
 * name before the opening brace
 * leaves the type usr value on the stack */
static int parse_record(struct parser *P, struct cp_ctype *ct)
{
	struct token tok;
	char cur_type_name[MAX_TYPE_NAME_LEN];

	require_token(P, &tok);

	/* name is optional */
	if (tok.type == TOK_TOKEN) {
		/* declaration */
		struct cp_ctype *lct;

		memset(cur_type_name, 0, MAX_TYPE_NAME_LEN);
		set_struct_type_name(cur_type_name, tok.str, tok.size);
;
		/* lookup the name to see if we've seen this type before */
		lct = ctype_lookup_type(cur_type_name);

		if (!lct) {
			/* new type, delay type registration to the end
			 * of this function */
		} else {
			/* get the exsting declared type */
			if (lct->type != ct->type) {
				cp_error("type '%s' previously declared as '%s'",
					cur_type_name,
					csym_name(ct_ffi_cs(lct)));
			}

			instantiate_typedef(P, ct, lct);
		}

		/* if a name is given then we may be at the end of the string
		 * eg for ffi.new('struct foo') */
		if (!next_token(P, &tok)) {
			return 0;
		}
	} else {
		/* create a new unnamed record */

		/*@TODO clean this up after unnamed record support is added */
		cp_error("TODO: support unnamed record.\n");
	}

	if (tok.type != TOK_OPEN_CURLY) {
		/* this may just be a declaration or use of the type as an
		 * argument or member */
		put_back(P);
		return 0;
	}

	if (ct->is_defined) {
		cp_error("redefinition in line %d", P->line);
		return 0;
	}

	if (ct->type == ENUM_TYPE) {
		parse_enum(P, ct);
	} else {
		/* we do a two stage parse, where we parse the content first
		 * and build up the temp user table. We then iterate over that
		 * to calculate the offsets and fill out ct_usr. This is so we
		 * can handle out of order members (eg vtable) and attributes
		 * specified at the end of the struct. */
		parse_struct(P, ct);
		/* build symbol for vm */
		ct->ffi_cs_id = cp_symbol_build_struct(cur_type_name);
		/* save cp_ctype for parser */
		cp_ctype_reg_type(cur_type_name, ct);
	}

	cp_set_defined(ct);
	return 0;
}

/* parses single or multi work built in types, and pushes it onto the stack */
static int parse_type_name(struct parser *P, char *type_name)
{
	struct token tok;
	int flags = 0;

	enum {
		UNSIGNED = 0x01,
		SIGNED = 0x02,
		LONG = 0x04,
		SHORT = 0x08,
		INT = 0x10,
		CHAR = 0x20,
		LONG_LONG = 0x40,
		INT8 = 0x80,
		INT16 = 0x100,
		INT32 = 0x200,
		INT64 = 0x400,
	};

	require_token(P, &tok);

	/* we have to manually decode the builtin types since they can take up
	 * more then one token */
	for (;;) {
		if (tok.type != TOK_TOKEN) {
			break;
		} else if (IS_LITERAL(tok, "unsigned")) {
			flags |= UNSIGNED;
		} else if (IS_LITERAL(tok, "signed")) {
			flags |= SIGNED;
		} else if (IS_LITERAL(tok, "short")) {
			flags |= SHORT;
		} else if (IS_LITERAL(tok, "char")) {
			flags |= CHAR;
		} else if (IS_LITERAL(tok, "long")) {
			flags |= (flags & LONG) ? LONG_LONG : LONG;
		} else if (IS_LITERAL(tok, "int")) {
			flags |= INT;
		} else if (IS_LITERAL(tok, "__int8")) {
			flags |= INT8;
		} else if (IS_LITERAL(tok, "__int16")) {
			flags |= INT16;
		} else if (IS_LITERAL(tok, "__int32")) {
			flags |= INT32;
		} else if (IS_LITERAL(tok, "__int64")) {
			flags |= INT64;
		} else if (IS_LITERAL(tok, "register")) {
			/* ignore */
		} else {
			break;
		}

		if (!next_token(P, &tok)) {
			break;
		}
	}

	if (flags) {
		put_back(P);
	}

	if (flags & CHAR) {
		if (flags & SIGNED) {
			strcpy(type_name, "int8_t");
		} else if (flags & UNSIGNED) {
			strcpy(type_name, "uint8_t");
		} else {
			if (((char) -1) > 0) {
				strcpy(type_name, "uint8_t");
			} else {
				strcpy(type_name, "int8_t");
			}
		}
	} else if (flags & INT8) {
		strcpy(type_name, (flags & UNSIGNED) ? "uint8_t" : "int8_t");
	} else if (flags & INT16) {
		strcpy(type_name, (flags & UNSIGNED) ? "uint16_t" : "int16_t");
	} else if (flags & INT32) {
		strcpy(type_name, (flags & UNSIGNED) ? "uint32_t" : "int32_t");
	} else if (flags & INT64) {
		strcpy(type_name, (flags & UNSIGNED) ? "uint64_t" : "int64_t");
	} else if (flags & LONG_LONG) {
		strcpy(type_name, (flags & UNSIGNED) ? "uint64_t" : "int64_t");
	} else if (flags & SHORT) {
#define SHORT_TYPE(u) (sizeof(short) == sizeof(int64_t) ? \
		u "int64_t" : sizeof(short) == sizeof(int32_t) ? \
		u "int32_t" : u "int16_t")
		if (flags & UNSIGNED) {
			strcpy(type_name, SHORT_TYPE("u"));
		} else {
			strcpy(type_name, SHORT_TYPE(""));
		}
#undef SHORT_TYPE
	} else if (flags & LONG) {
#define LONG_TYPE(u) (sizeof(long) == sizeof(int64_t) ? \
		u "int64_t" : u "int32_t")
		if (flags & UNSIGNED) {
			strcpy(type_name, LONG_TYPE("u"));
		} else {
			strcpy(type_name, LONG_TYPE(""));
		}
#undef LONG_TYPE
	} else if (flags) {
#define INT_TYPE(u) (sizeof(int) == sizeof(int64_t) ? \
		u "int64_t" : sizeof(int) == sizeof(int32_t) ? \
		u "int32_t" : u "int16_t")
		if (flags & UNSIGNED) {
			strcpy(type_name, INT_TYPE("u"));
		} else {
			strcpy(type_name, INT_TYPE(""));
		}
#undef INT_TYPE
	} else {
		strncpy(type_name, tok.str, tok.size);
	}

	return 0;
}

/* parse_attribute parses a token to see if it is an attribute. It may then
 * parse some following tokens to decode the attribute setting the appropriate
 * fields in ct. It will return 1 if the token was used (and possibly some
 * more following it) or 0 if not. If the token was used, the next token must
 * be retrieved using next_token/require_token. */
static int parse_attribute(struct parser *P, struct token *tok,
		struct cp_ctype *ct, struct parser *asmname)
{
	if (tok->type != TOK_TOKEN) {
		return 0;
	} else if (asmname && (IS_LITERAL(*tok, "__asm__")
				|| IS_LITERAL(*tok, "__asm"))) {
		check_token(P, TOK_OPEN_PAREN, NULL,
				"unexpected token after __asm__ on line %d",
				P->line);
		*asmname = *P;

		require_token(P, tok);
		while (tok->type == TOK_STRING) {
			require_token(P, tok);
		}

		if (tok->type != TOK_CLOSE_PAREN) {
			cp_error("unexpected token after __asm__ on line %d",
					P->line);
		}
		return 1;

	} else if (IS_LITERAL(*tok, "__attribute__")
			|| IS_LITERAL(*tok, "__declspec")) {
		int parens = 1;
		check_token(P, TOK_OPEN_PAREN, NULL,
				"expected parenthesis after __attribute__ or "
				"__declspec on line %d", P->line);

		for (;;) {
			require_token(P, tok);
			if (tok->type == TOK_OPEN_PAREN) {
				parens++;
			} else if (tok->type == TOK_CLOSE_PAREN) {
				if (--parens == 0) {
					break;
				}

			} else if (tok->type != TOK_TOKEN) {
				/* ignore unknown symbols within parentheses */

			} else if (IS_LITERAL(*tok, "align") ||
					IS_LITERAL(*tok, "aligned") ||
					IS_LITERAL(*tok, "__aligned__")) {
				unsigned align = 0;
				require_token(P, tok);

				switch (tok->type) {
				case TOK_CLOSE_PAREN:
					align = ALIGNED_DEFAULT;
					put_back(P);
					break;

				case TOK_OPEN_PAREN:
					require_token(P, tok);

					if (tok->type != TOK_NUMBER) {
						cp_error("expected align(#) "
							"on line %d", P->line);
					}

					switch (tok->integer) {
					case 1: align = 0; break;
					case 2: align = 1; break;
					case 4: align = 3; break;
					case 8: align = 7; break;
					case 16: align = 15; break;
					default:
						cp_error("unsupported align "
							"size on line %d",
							P->line);
					}

					check_token(P, TOK_CLOSE_PAREN, NULL,
						"expected align(#) on line %d",
						P->line);
					break;

				default:
					cp_error("expected align(#) on line %d",
							P->line);
				}

				/* __attribute__(aligned(#)) is only supposed
				 * to increase alignment */
				ct->align_mask = max(align, ct->align_mask);

			} else if (IS_LITERAL(*tok, "packed")
					|| IS_LITERAL(*tok, "__packed__")) {
				ct->align_mask = 0;
				ct->is_packed = 1;

			} else if (IS_LITERAL(*tok, "mode")
					|| IS_LITERAL(*tok, "__mode__")) {

				check_token(P, TOK_OPEN_PAREN, NULL,
					"expected mode(MODE) on line %d",
					P->line);

				require_token(P, tok);
				if (tok->type != TOK_TOKEN) {
					cp_error("expected mode(MODE) on line %d",
							P->line);
				}


				struct {char ch; uint16_t v;} a16;
				struct {char ch; uint32_t v;} a32;
				struct {char ch; uint64_t v;} a64;

				if (IS_LITERAL(*tok, "QI")
						|| IS_LITERAL(*tok, "__QI__")
						|| IS_LITERAL(*tok, "byte")
						|| IS_LITERAL(*tok, "__byte__")
				   ) {
					ct->type = INT8_TYPE;
					ct->base_size = sizeof(uint8_t);
					ct->align_mask = 0;

				} else if (IS_LITERAL(*tok, "HI")
						|| IS_LITERAL(*tok, "__HI__")) {
					ct->type = INT16_TYPE;
					ct->base_size = sizeof(uint16_t);
					ct->align_mask = ALIGNOF(a16);

				} else if (IS_LITERAL(*tok, "SI")
						|| IS_LITERAL(*tok, "__SI__")
#if defined ARCH_X86 || defined ARCH_ARM
						|| IS_LITERAL(*tok, "word")
						|| IS_LITERAL(*tok, "__word__")
						|| IS_LITERAL(*tok, "pointer")
						|| IS_LITERAL(*tok, "__pointer__")
#endif
					  ) {
					ct->type = INT32_TYPE;
					ct->base_size = sizeof(uint32_t);
					ct->align_mask = ALIGNOF(a32);

				} else if (IS_LITERAL(*tok, "DI")
						|| IS_LITERAL(*tok, "__DI__")
#if defined ARCH_X64
						|| IS_LITERAL(*tok, "word")
						|| IS_LITERAL(*tok, "__word__")
						|| IS_LITERAL(*tok, "pointer")
						|| IS_LITERAL(*tok, "__pointer__")
#endif
					  ) {
					ct->type = INT64_TYPE;
					ct->base_size = sizeof(uint64_t);
					ct->align_mask = ALIGNOF(a64);

				} else {
					cp_error("unexpected mode on line %d",
							P->line);
				}

				check_token(P, TOK_CLOSE_PAREN, NULL,
					"expected mode(MODE) on line %d", P->line);

			} else if (IS_LITERAL(*tok, "cdecl")
					|| IS_LITERAL(*tok, "__cdecl__")) {
				ct->calling_convention = C_CALL;

			} else if (IS_LITERAL(*tok, "fastcall")
					|| IS_LITERAL(*tok, "__fastcall__")) {
				ct->calling_convention = FAST_CALL;

			} else if (IS_LITERAL(*tok, "stdcall")
					|| IS_LITERAL(*tok, "__stdcall__")) {
				ct->calling_convention = STD_CALL;
			}
			/* ignore unknown tokens within parentheses */
		}
		return 1;

	} else if (IS_LITERAL(*tok, "__cdecl")) {
		ct->calling_convention = C_CALL;
		return 1;

	} else if (IS_LITERAL(*tok, "__fastcall")) {
		ct->calling_convention = FAST_CALL;
		return 1;

	} else if (IS_LITERAL(*tok, "__stdcall")) {
		ct->calling_convention = STD_CALL;
		return 1;

	} else if (IS_LITERAL(*tok, "__extension__")
			|| IS_LITERAL(*tok, "extern")) {
		/* ignore */
		return 1;
	} else {
		return 0;
	}
}

/* parses from after the opening paranthesis to after the closing parenthesis */
static void parse_function_arguments(struct parser* P, struct cp_ctype* ct)
{
	struct token tok;
	int args = 0;

	for (;;) {
		require_token(P, &tok);

		if (tok.type == TOK_CLOSE_PAREN)
			break;

		if (args) {
			if (tok.type != TOK_COMMA) {
				cp_error("unexpected token in function "
						"argument %d on line %d",
						args, P->line);
			}
			require_token(P, &tok);
		}

		if (tok.type == TOK_VA_ARG) {
			ct->has_var_arg = true;
			check_token(P, TOK_CLOSE_PAREN, "",
					"unexpected token after ... in "
					"function on line %d",
					P->line);
			break;
		} else if (tok.type == TOK_TOKEN) {
			struct cp_ctype at;

			put_back(P);
			parse_type(P, &at);
			parse_argument(P, &at, NULL, NULL);

			/* array arguments are just treated as their
			 * base pointer type */
			at.is_array = 0;

			/* check for the c style int func(void) and error
			 * on other uses of arguments of type void */
			if (at.type == VOID_TYPE && at.pointers == 0) {
				if (args) {
					cp_error("can't have argument of type "
							"void on line %d",
							P->line);
				}

				check_token(P, TOK_CLOSE_PAREN, "",
					"unexpected void in function on line %d",
					P->line);
				break;
			}
			cp_push_ctype(&at);
			args++;
		} else {
			cp_error("unexpected token in function argument %d "
					"on line %d", args+1, P->line);
		}
	}
}

static int max_bitfield_size(int type)
{
	switch (type) {
	case BOOL_TYPE:
		return 1;
	case INT8_TYPE:
		return 8;
	case INT16_TYPE:
		return 16;
	case INT32_TYPE:
	case ENUM_TYPE:
		return 32;
	case INT64_TYPE:
		return 64;
	default:
		return -1;
	}
}

static struct cp_ctype *parse_argument2(struct parser *P, struct cp_ctype *ct,
		struct token *name, struct parser *asmname);

/* parses from after the first ( in a function declaration or function pointer
 * can be one of:
 * void foo(...) before ...
 * void (foo)(...) before foo
 * void (* <>)(...) before <> which is the inner type */
static struct cp_ctype *parse_function(struct parser *P, struct cp_ctype *ct,
				struct token *name, struct parser *asmname)
{
	/* We have a function pointer or a function. The usr table will
	 * get replaced by the canonical one (if there is one) in
	 * find_canonical_usr after all the arguments and returns have
	 * been parsed. */
	struct token tok;
	struct cp_ctype *ret = ct;

	cp_push_ctype(ct);

	memset(ct, 0, sizeof(*ct));
	ct->base_size = sizeof(void (*)());
	ct->align_mask = min(FUNCTION_ALIGN_MASK, P->align_mask);
	ct->type = FUNCTION_TYPE;
	ct->is_defined = 1;

	if (name->type == TOK_NIL) {
		for (;;) {
			require_token(P, &tok);

			if (tok.type == TOK_STAR) {
				if (ct->type == FUNCTION_TYPE) {
					ct->type = FUNCTION_PTR_TYPE;
				} else {
					increase_ptr_deref_level(P, ct);
				}
			} else if (parse_attribute(P, &tok, ct, asmname)) {
				/* parse_attribute sets the appropriate fields */
			} else {
				/* call parse_argument to handle the inner
				 * contents e.g. the <> in "void (* <>)
				 * (...)". Note that the inner contents can
				 * itself be a function, a function ptr,
				 * array, etc (e.g. "void (*signal(int sig,
				 * void (*func)(int)))(int)" ). */
				cp_error("TODO: inner function not supported for now.");
				put_back(P);
				ct = parse_argument2(P, ct, name, asmname);
				break;
			}
		}

		check_token(P, TOK_CLOSE_PAREN, NULL,
			"unexpected token in function on line %d", P->line);
		check_token(P, TOK_OPEN_PAREN, NULL,
			"unexpected token in function on line %d", P->line);
	}

	parse_function_arguments(P, ct);

	/*@TODO support for inner function  24.11 2013 (houqp)*/
	/* if we have an inner function then set the outer function ptr as its
	 * return type and return the inner function
	 * e.g. for void (* <signal(int, void (*)(int))> )(int) inner is
	 * surrounded by <>, return type is void (*)(int) */

	return ret;
}

static struct cp_ctype *parse_argument2(struct parser *P, struct cp_ctype *ct,
				struct token *name, struct parser *asmname)
{
	struct token tok;

	for (;;) {
		if (!next_token(P, &tok)) {
			/* we've reached the end of the string */
			break;
		} else if (tok.type == TOK_STAR) {
			increase_ptr_deref_level(P, ct);

			/* __declspec(align(#)) may come before the type in a
			 * member */
			if (!ct->is_packed) {
				ct->align_mask = max(min(PTR_ALIGN_MASK, P->align_mask),
							ct->align_mask);
			}
		} else if (tok.type == TOK_REFERENCE) {
			cp_error("NYI: c++ reference types");
			return 0;
		} else if (parse_attribute(P, &tok, ct, asmname)) {
			/* parse attribute has filled out appropriate fields in type */

		} else if (tok.type == TOK_OPEN_PAREN) {
			ct = parse_function(P, ct, name, asmname);
		} else if (tok.type == TOK_OPEN_SQUARE) {
			/* array */
			if (ct->pointers == POINTER_MAX) {
				cp_error("maximum number of pointer derefs "
					"reached - use a struct to break up "
					"the pointers");
			}
			ct->is_array = 1;
			ct->pointers++;
			ct->const_mask <<= 1;
			require_token(P, &tok);

			if (ct->pointers == 1 && !ct->is_defined) {
				cp_error("array of undefined type on line %d",
						P->line);
			}

			if (ct->is_variable_struct || ct->is_variable_array) {
				cp_error("can't have an array of a variably "
					"sized type on line %d", P->line);
			}

			if (tok.type == TOK_QUESTION) {
				ct->is_variable_array = 1;
				ct->variable_increment = (ct->pointers > 1) ?
						sizeof(void*) : ct->base_size;
				check_token(P, TOK_CLOSE_SQUARE, "",
					"invalid character in array on line %d",
					P->line);

			} else if (tok.type == TOK_CLOSE_SQUARE) {
				ct->array_size = 0;

			} else if (tok.type == TOK_TOKEN && IS_RESTRICT(tok)) {
				/* odd gcc extension foo[__restrict] for arguments */
				ct->array_size = 0;
				check_token(P, TOK_CLOSE_SQUARE, "",
					"invalid character in array on line %d",
					P->line);
			} else {
				int64_t asize;
				put_back(P);
				asize = calculate_constant(P);
				if (asize < 0) {
					cp_error("array size can not be "
						"negative on line %d", P->line);
					return 0;
				}
				ct->array_size = (size_t) asize;
				check_token(P, TOK_CLOSE_SQUARE, "",
					"invalid character in array on line %d",
					P->line);
			}

		} else if (tok.type == TOK_COLON) {
			int64_t bsize = calculate_constant(P);

			if (ct->pointers || bsize < 0
					|| bsize > max_bitfield_size(ct->type)) {
				cp_error("invalid bitfield on line %d", P->line);
			}

			ct->is_bitfield = 1;
			ct->bit_size = (unsigned) bsize;

		} else if (tok.type != TOK_TOKEN) {
			/* we've reached the end of the declaration */
			put_back(P);
			break;

		} else if (IS_CONST(tok)) {
			ct->const_mask |= 1;

		} else if (IS_VOLATILE(tok) || IS_RESTRICT(tok)) {
			/* ignored for now */

		} else {
			*name = tok;
		}
	}

	return ct;
}



/* parses after the main base type of a typedef, function argument or
 * struct/union member
 * eg for const void* bar[3] the base type is void with the subtype so far of
 * const, this parses the "* bar[3]" and updates the type argument
 *
 * type must be as filled out by parse_type
 *
 * pushes the updated user value on the top of the stack
 */
void parse_argument(struct parser *P, struct cp_ctype *ct, struct token *pname,
			struct parser *asmname)
{
	struct token tok, name;

	memset(&name, 0, sizeof(name));
	parse_argument2(P, ct, &name, asmname);

	for (;;) {
		if (!next_token(P, &tok)) {
			break;
		} else if (parse_attribute(P, &tok, ct, asmname)) {
			/* parse_attribute sets the appropriate fields */
		} else {
			put_back(P);
			break;
		}
	}

	if (pname) {
		*pname = name;
	}
}

static void parse_typedef(struct parser *P)
{
	struct token tok;
	struct cp_ctype base_type;
	char typedef_name[MAX_TYPE_NAME_LEN];

	parse_type(P, &base_type);

	for (;;) {
		struct cp_ctype arg_type = base_type;
		struct token name;

		memset(&name, 0, sizeof(name));

		parse_argument(P, &arg_type, &name, NULL);

		if (!name.size) {
			cp_error("Can't have a typedef without a name on line %d",
					P->line);
		} else if (arg_type.is_variable_array) {
			cp_error("Can't typedef a variable length array on line %d",
					P->line);
		}

		memset(typedef_name, 0, sizeof(typedef_name));
		strncpy(typedef_name, name.str, name.size);
		/* link typedef name with ctype for parser */
		cp_ctype_reg_type(typedef_name, &arg_type);

		require_token(P, &tok);

		if (tok.type == TOK_SEMICOLON) {
			break;
		} else if (tok.type != TOK_COMMA) {
			cp_error("Unexpected character in typedef on line %d",
					P->line);
		}
	}
}

#define END 0
#define PRAGMA_POP 1

static int parse_root(struct parser *P)
{
	struct token tok;

	while (next_token(P, &tok)) {
		/* we can have:
		 * struct definition
		 * enum definition
		 * union definition
		 * struct/enum/union declaration
		 * typedef
		 * function declaration
		 * pragma pack
		 */

		if (tok.type == TOK_SEMICOLON) {
			/* empty semicolon in root continue on */

		} else if (tok.type == TOK_POUND) {

			check_token(P, TOK_TOKEN, "pragma",
				"unexpected pre processor directive on line %d",
				P->line);
			check_token(P, TOK_TOKEN, "pack",
				"unexpected pre processor directive on line %d",
				P->line);
			check_token(P, TOK_OPEN_PAREN, "",
				"invalid pack directive on line %d",
				P->line);
			require_token(P, &tok);

			if (tok.type == TOK_NUMBER) {
				if (tok.integer != 1 && tok.integer != 2
						&& tok.integer != 4
						&& tok.integer != 8
						&& tok.integer != 16) {
					cp_error("pack directive with invalid "
							"pack size on line %d",
							P->line);
					return 0;
				}

				P->align_mask = (unsigned) (tok.integer - 1);
				check_token(P, TOK_CLOSE_PAREN, "",
					"invalid pack directive on line %d",
					P->line);
			} else if (tok.type == TOK_TOKEN && IS_LITERAL(tok, "push")) {
				/*int line = P->line;*/
				unsigned previous_alignment = P->align_mask;

				check_token(P, TOK_CLOSE_PAREN, "",
					"invalid pack directive on line %d",
					P->line);

				if (parse_root(P) != PRAGMA_POP) {
					cp_error("reached end of string "
						"without a pragma pop to "
						"match the push on line %d",
						P->line);
					return 0;
				}

				P->align_mask = previous_alignment;

			} else if (tok.type == TOK_TOKEN && IS_LITERAL(tok, "pop")) {
				check_token(P, TOK_CLOSE_PAREN, "",
					"invalid pack directive on line %d",
						P->line);
				return PRAGMA_POP;
			} else {
				cp_error("invalid pack directive on line %d",
						P->line);
				return 0;
			}
		} else if (tok.type != TOK_TOKEN) {
			cp_error("unexpected character on line %d", P->line);
			return 0;
		} else if (IS_LITERAL(tok, "__extension__")) {
			/* ignore */
			continue;
		} else if (IS_LITERAL(tok, "extern")) {
			/* ignore extern as data and functions can only be
			 * extern */
			continue;
		} else if (IS_LITERAL(tok, "typedef")) {
			parse_typedef(P);
		} else if (IS_LITERAL(tok, "static")) {
			/*@TODO we haven't tested static so far */
			cp_error("TODO: support static keyword.\n");
		} else {
			/* type declaration, type definition, or function
			 * declaration */
			struct cp_ctype type;
			struct token name;
			struct parser asmname;

			memset(&name, 0, sizeof(name));
			memset(&asmname, 0, sizeof(asmname));

			put_back(P);
			parse_type(P, &type);

			for (;;) {
				parse_argument(P, &type, &name, &asmname);

				if (name.size) {
					/* global/function declaration */
					cp_symbol_build_func(&type, name.str, name.size);
					/* @TODO asmname is not used for now
					 * since we are not supporting __asm__
					 * as this point.
					 * might need to bind it with function
					 * name later. */
				} else {
					/* type declaration/definition -
					 * already been processed */
				}
				require_token(P, &tok);

				if (tok.type == TOK_SEMICOLON) {
					break;
				} else if (tok.type != TOK_COMMA) {
					cp_error("missing semicolon on line %d",
							P->line);
				}
			}
		}
	}

	return END;
}

static int64_t calculate_constant2(struct parser *P, struct token *tok);

/* () */
static int64_t calculate_constant1(struct parser *P, struct token *tok)
{
	int64_t ret;

	if (tok->type == TOK_NUMBER) {
		ret = tok->integer;
		next_token(P, tok);
		return ret;

	} else if (tok->type == TOK_TOKEN) {
		/* look up name in constants table */
		cp_error("TODO: support name lookup in constant table\n");
		next_token(P, tok);
		return ret;

	} else if (tok->type == TOK_OPEN_PAREN) {
		struct parser before_cast = *P;
		cp_error("TODO: handle open parent token in constant1\n");
		*P = before_cast;
		ret = calculate_constant(P);

		require_token(P, tok);
		if (tok->type != TOK_CLOSE_PAREN) {
			cp_error("error whilst parsing constant at line %d",
					P->line);
		}

		next_token(P, tok);
		return ret;
	} else {
		cp_error("unexpected token whilst parsing constant at line %d",
				P->line);
		return 0;
	}
}

/* ! and ~, unary + and -, and sizeof */
static int64_t calculate_constant2(struct parser *P, struct token *tok)
{
	if (tok->type == TOK_LOGICAL_NOT) {
		require_token(P, tok);
		return !calculate_constant2(P, tok);

	} else if (tok->type == TOK_BITWISE_NOT) {
		require_token(P, tok);
		return ~calculate_constant2(P, tok);

	} else if (tok->type == TOK_PLUS) {
		require_token(P, tok);
		return calculate_constant2(P, tok);

	} else if (tok->type == TOK_MINUS) {
		require_token(P, tok);
		return -calculate_constant2(P, tok);

	} else if (tok->type == TOK_TOKEN &&
			(IS_LITERAL(*tok, "sizeof")
			 || IS_LITERAL(*tok, "alignof")
			 || IS_LITERAL(*tok, "__alignof__")
			 || IS_LITERAL(*tok, "__alignof"))) {
		cp_error("TODO: support sizeof\n");
		bool issize = IS_LITERAL(*tok, "sizeof");
		struct cp_ctype type;

		require_token(P, tok);
		if (tok->type != TOK_OPEN_PAREN) {
			cp_error("invalid sizeof at line %d", P->line);
		}

		parse_type(P, &type);
		parse_argument(P, &type, NULL, NULL);

		require_token(P, tok);
		if (tok->type != TOK_CLOSE_PAREN) {
			cp_error("invalid sizeof at line %d", P->line);
		}

		next_token(P, tok);

		return issize ? ctype_size(&type) : type.align_mask + 1;

	} else {
		return calculate_constant1(P, tok);
	}
}

/* binary * / and % (left associative) */
static int64_t calculate_constant3(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant2(P, tok);

	for (;;) {
		if (tok->type == TOK_MULTIPLY) {
			require_token(P, tok);
			left *= calculate_constant2(P, tok);

		} else if (tok->type == TOK_DIVIDE) {
			require_token(P, tok);
			left /= calculate_constant2(P, tok);

		} else if (tok->type == TOK_MODULUS) {
			require_token(P, tok);
			left %= calculate_constant2(P, tok);

		} else {
			return left;
		}
	}
}

/* binary + and - (left associative) */
static int64_t calculate_constant4(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant3(P, tok);

	for (;;) {
		if (tok->type == TOK_PLUS) {
			require_token(P, tok);
			left += calculate_constant3(P, tok);

		} else if (tok->type == TOK_MINUS) {
			require_token(P, tok);
			left -= calculate_constant3(P, tok);

		} else {
			return left;
		}
	}
}

/* binary << and >> (left associative) */
static int64_t calculate_constant5(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant4(P, tok);

	for (;;) {
		if (tok->type == TOK_LEFT_SHIFT) {
			require_token(P, tok);
			left <<= calculate_constant4(P, tok);

		} else if (tok->type == TOK_RIGHT_SHIFT) {
			require_token(P, tok);
			left >>= calculate_constant4(P, tok);

		} else {
			return left;
		}
	}
}

/* binary <, <=, >, and >= (left associative) */
static int64_t calculate_constant6(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant5(P, tok);

	for (;;) {
		if (tok->type == TOK_LESS) {
			require_token(P, tok);
			left = (left < calculate_constant5(P, tok));

		} else if (tok->type == TOK_LESS_EQUAL) {
			require_token(P, tok);
			left = (left <= calculate_constant5(P, tok));

		} else if (tok->type == TOK_GREATER) {
			require_token(P, tok);
			left = (left > calculate_constant5(P, tok));

		} else if (tok->type == TOK_GREATER_EQUAL) {
			require_token(P, tok);
			left = (left >= calculate_constant5(P, tok));

		} else {
			return left;
		}
	}
}

/* binary ==, != (left associative) */
static int64_t calculate_constant7(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant6(P, tok);

	for (;;) {
		if (tok->type == TOK_EQUAL) {
			require_token(P, tok);
			left = (left == calculate_constant6(P, tok));

		} else if (tok->type == TOK_NOT_EQUAL) {
			require_token(P, tok);
			left = (left != calculate_constant6(P, tok));

		} else {
			return left;
		}
	}
}

/* binary & (left associative) */
static int64_t calculate_constant8(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant7(P, tok);

	for (;;) {
		if (tok->type == TOK_BITWISE_AND) {
			require_token(P, tok);
			left = (left & calculate_constant7(P, tok));

		} else {
			return left;
		}
	}
}

/* binary ^ (left associative) */
static int64_t calculate_constant9(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant8(P, tok);

	for (;;) {
		if (tok->type == TOK_BITWISE_XOR) {
			require_token(P, tok);
			left = (left ^ calculate_constant8(P, tok));

		} else {
			return left;
		}
	}
}

/* binary | (left associative) */
static int64_t calculate_constant10(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant9(P, tok);

	for (;;) {
		if (tok->type == TOK_BITWISE_OR) {
			require_token(P, tok);
			left = (left | calculate_constant9(P, tok));

		} else {
			return left;
		}
	}
}

/* binary && (left associative) */
static int64_t calculate_constant11(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant10(P, tok);

	for (;;) {
		if (tok->type == TOK_LOGICAL_AND) {
			require_token(P, tok);
			left = (left && calculate_constant10(P, tok));

		} else {
			return left;
		}
	}
}

/* binary || (left associative) */
static int64_t calculate_constant12(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant11(P, tok);

	for (;;) {
		if (tok->type == TOK_LOGICAL_OR) {
			require_token(P, tok);
			left = (left || calculate_constant11(P, tok));

		} else {
			return left;
		}
	}
}

/* ternary ?: (right associative) */
static int64_t calculate_constant13(struct parser *P, struct token *tok)
{
	int64_t left = calculate_constant12(P, tok);

	if (tok->type == TOK_QUESTION) {
		int64_t middle, right;
		require_token(P, tok);
		middle = calculate_constant13(P, tok);
		if (tok->type != TOK_COLON) {
			cp_error("invalid ternery (? :) in constant on line %d",
					P->line);
		}
		require_token(P, tok);
		right = calculate_constant13(P, tok);
		return left ? middle : right;

	} else {
		return left;
	}
}

int64_t calculate_constant(struct parser* P)
{
	struct token tok;
	int64_t ret;
	require_token(P, &tok);
	ret = calculate_constant13(P, &tok);

	if (tok.type != TOK_NIL) {
		put_back(P);
	}

	return ret;
}

int ffi_cdef(const char *s)
{
	struct parser P;

	memset(&P, 0, sizeof(struct parser));
	P.line = 1;
	P.prev = P.next = s;
	P.align_mask = DEFAULT_ALIGN_MASK;

	if (parse_root(&P) == PRAGMA_POP) {
		cp_error("pragma pop without an associated push on line %d",
				P.line);
	}

	return 0;
}

void ffi_cparser_init(void)
{
	cp_ctype_init();
}

void ffi_cparser_free(void)
{
	cp_ctype_free();
}
