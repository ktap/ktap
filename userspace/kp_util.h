#ifndef __KTAP_UTIL_H__
#define __KTAP_UTIL_H__

#include <stdarg.h>

#include "../include/ktap_bc.h"
#include "../include/ktap_err.h"

typedef int bool;
#define false 0
#define true 1

/* Resizable string buffer. */
typedef struct SBuf {
	char *p; /* String buffer pointer. */
	char *e; /* String buffer end pointer. */
	char *b; /* String buffer base. */
} SBuf;

/* Resizable string buffers. Struct definition in kp_obj.h. */
#define sbufB(sb)	((char *)(sb)->b)
#define sbufP(sb)	((char *)(sb)->p)
#define sbufE(sb)	((char *)(sb)->e)
#define sbufsz(sb)	((int)(sbufE((sb)) - sbufB((sb))))
#define sbuflen(sb)	((int)(sbufP((sb)) - sbufB((sb))))
#define sbufleft(sb)	((int)(sbufE((sb)) - sbufP((sb))))
#define setsbufP(sb, q) ((sb)->p = (q))

void kp_buf_init(SBuf *sb);
void kp_buf_reset(SBuf *sb);
void kp_buf_free(SBuf *sb);
char *kp_buf_more(SBuf *sb, int sz);
char *kp_buf_need(SBuf *sb, int sz);
char *kp_buf_wmem(char *p, const void *q, int len);
void kp_buf_putb(SBuf *sb, int c);
ktap_str_t *kp_buf_str(SBuf *sb);


#define KP_CHAR_CNTRL	0x01
#define KP_CHAR_SPACE	0x02
#define KP_CHAR_PUNCT	0x04
#define KP_CHAR_DIGIT	0x08
#define KP_CHAR_XDIGIT	0x10
#define KP_CHAR_UPPER	0x20
#define KP_CHAR_LOWER	0x40
#define KP_CHAR_IDENT	0x80
#define KP_CHAR_ALPHA	(KP_CHAR_LOWER|KP_CHAR_UPPER)
#define KP_CHAR_ALNUM	(KP_CHAR_ALPHA|KP_CHAR_DIGIT)
#define KP_CHAR_GRAPH	(KP_CHAR_ALNUM|KP_CHAR_PUNCT)

/* Only pass -1 or 0..255 to these macros. Never pass a signed char! */
#define kp_char_isa(c, t)	((kp_char_bits+1)[(c)] & t)
#define kp_char_iscntrl(c)	kp_char_isa((c), KP_CHAR_CNTRL)
#define kp_char_isspace(c)	kp_char_isa((c), KP_CHAR_SPACE)
#define kp_char_ispunct(c)	kp_char_isa((c), KP_CHAR_PUNCT)
#define kp_char_isdigit(c)	kp_char_isa((c), KP_CHAR_DIGIT)
#define kp_char_isxdigit(c)	kp_char_isa((c), KP_CHAR_XDIGIT)
#define kp_char_isupper(c)	kp_char_isa((c), KP_CHAR_UPPER)
#define kp_char_islower(c)	kp_char_isa((c), KP_CHAR_LOWER)
#define kp_char_isident(c)	kp_char_isa((c), KP_CHAR_IDENT)
#define kp_char_isalpha(c)	kp_char_isa((c), KP_CHAR_ALPHA)
#define kp_char_isalnum(c)	kp_char_isa((c), KP_CHAR_ALNUM)
#define kp_char_isgraph(c)	kp_char_isa((c), KP_CHAR_GRAPH)

#define kp_char_toupper(c)	((c) - (kp_char_islower(c) >> 1))
#define kp_char_tolower(c)	((c) + kp_char_isupper(c))

extern const char *kp_err_allmsg;
#define err2msg(em)     (kp_err_allmsg+(int)(em))

extern const uint8_t kp_char_bits[257];


char *strfmt_wuleb128(char *p, uint32_t v);
void kp_err_lex(ktap_str_t *src, const char *tok, BCLine line,
		ErrMsg em, va_list argp);
char *kp_sprintf(const char *fmt, ...);
const char *kp_sprintfv(const char *fmt, va_list argp);

void *kp_reallocv(void *block, size_t osize, size_t nsize);

void kp_str_resize(void);
ktap_str_t *kp_str_newz(const char *str);
ktap_str_t *kp_str_new(const char *str, size_t l);

ktap_tab_t *kp_tab_new();
const ktap_val_t *kp_tab_get(ktap_tab_t *t, const ktap_val_t *key);
const ktap_val_t *kp_tab_getstr(ktap_tab_t *t, const ktap_str_t *ts);
void kp_tab_setvalue(ktap_tab_t *t, const ktap_val_t *key, ktap_val_t *val);
ktap_val_t *kp_tab_set(ktap_tab_t *t, const ktap_val_t *key);

int kp_obj_equal(const ktap_val_t *t1, const ktap_val_t *t2);

bool strglobmatch(const char *str, const char *pat);
int kallsyms_parse(void *arg,
		   int(*process_symbol)(void *arg, const char *name,
		   char type, unsigned long start));

unsigned long find_kernel_symbol(const char *symbol);
void list_available_events(const char *match);
void process_available_tracepoints(const char *sys, const char *event,
				   int (*process)(const char *sys,
						  const char *event));
int kallsyms_parse(void *arg,
                   int(*process_symbol)(void *arg, const char *name,
                   char type, unsigned long start));


#ifdef CONFIG_KTAP_FFI
#include "../include/ktap_ffi.h"

typedef struct cp_csymbol_state {
	int cs_nr; /* number of c symbols */
	int cs_arr_size; /* size of current symbol arrays */
	csymbol *cs_arr;
} cp_csymbol_state;

cp_csymbol_state *ctype_get_csym_state(void);
void kp_dump_csymbols();
#endif

ktap_eventdesc_t *kp_parse_events(const char *eventdef);
void cleanup_event_resources(void);

extern int verbose;
#define verbose_printf(...) \
	if (verbose)	\
		printf("[verbose] " __VA_ARGS__);


void kp_dump_proto(ktap_proto_t *pt);
typedef int (*ktap_writer)(const void* p, size_t sz, void* ud);
int kp_bcwrite(ktap_proto_t *pt, ktap_writer writer, void *data, int strip);

int kp_create_reader(const char *output);
#endif
