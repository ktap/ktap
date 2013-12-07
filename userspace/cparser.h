#ifndef __KTAP_CPARSER_H__
#define __KTAP_CPARSER_H__

/*
 * Copyright (c) 2011 James R. McKaskill
 *
 * This software is licensed under the stock MIT license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * ----------------------------------------------------------------------------
 */

/*
 * Adapted from luaffi commit: abc638c9341025580099dcf77795c4b320ba0e63
 *
 * Copyright (c) 2013 Yicheng Qin, Qingping Hou
 */

#ifdef CONFIG_KTAP_FFI

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "../include/ktap_ffi.h"

#define PTR_ALIGN_MASK (sizeof(void*) - 1)
#define FUNCTION_ALIGN_MASK (sizeof(void (*)()) - 1)
#define DEFAULT_ALIGN_MASK 7

struct parser {
	int line;
	const char *next;
	const char *prev;
	unsigned align_mask;
};

enum {
	C_CALL,
	STD_CALL,
	FAST_CALL,
};


#define MAX_TYPE_NAME_LEN CSYM_NAME_MAX_LEN

enum {
	/* 0 - 4 */
	INVALID_TYPE,
	VOID_TYPE,
	BOOL_TYPE,
	INT8_TYPE,
	INT16_TYPE,
	/* 5 - 9 */
	INT32_TYPE,
	INT64_TYPE,
	INTPTR_TYPE,
	ENUM_TYPE,
	UNION_TYPE,
	/* 10 - 12 */
	STRUCT_TYPE,
	FUNCTION_TYPE,
	FUNCTION_PTR_TYPE,
};


#define IS_CHAR_UNSIGNED (((char) -1) > 0)

#define POINTER_BITS 2
#define POINTER_MAX ((1 << POINTER_BITS) - 1)

#define ALIGNOF(S) ((int) ((char*) &S.v - (char*) &S - 1))


/* Note: if adding a new member that is associated with a struct/union
 * definition then it needs to be copied over in ctype.c:set_defined for when
 * we create types based off of the declaration alone.
 *
 * Since this is used as a header for every ctype and cdata, and we create a
 * ton of them on the stack, we try and minimise its size.
 */
struct cp_ctype {
	size_t base_size; /* size of the base type in bytes */
	int ffi_cs_id; /* index for csymbol from ktap vm */
	union {
		/* valid if is_bitfield */
		struct {
			/* size of bitfield in bits */
			unsigned bit_size : 7;
			/* offset within the current byte between 0-63 */
			unsigned bit_offset : 6;
		};
		/* Valid if is_array */
		size_t array_size;
		/* Valid for is_variable_struct or is_variable_array. If
		 * variable_size_known (only used for is_variable_struct)
		 * then this is the total increment otherwise this is the
		 * per element increment.
		 */
		size_t variable_increment;
	};
	size_t offset;
	/* as (align bytes - 1) eg 7 gives 8 byte alignment */
	unsigned align_mask : 4;
	/* number of dereferences to get to the base type
	 * including +1 for arrays */
	unsigned pointers : POINTER_BITS;
	/* const pointer mask, LSB is current pointer, +1 for the whether
	 * the base type is const */
	unsigned const_mask : POINTER_MAX + 1;
	unsigned type : 5; /* value given by type enum above */
	unsigned is_reference : 1;
	unsigned is_array : 1;
	unsigned is_defined : 1;
	unsigned is_null : 1;
	unsigned has_member_name : 1;
	unsigned calling_convention : 2;
	unsigned has_var_arg : 1;
	/* set for variable array types where we don't know
	 * the variable size yet */
	unsigned is_variable_array : 1;
	unsigned is_variable_struct : 1;
	/* used for variable structs after we know the variable size */
	unsigned variable_size_known : 1;
	unsigned is_bitfield : 1;
	unsigned has_bitfield : 1;
	unsigned is_jitted : 1;
	unsigned is_packed : 1;
	unsigned is_unsigned : 1;
};

#define ALIGNED_DEFAULT (__alignof__(void* __attribute__((aligned))) - 1)

csymbol *cp_id_to_csym(int id);
#define ct_ffi_cs(ct) (cp_id_to_csym((ct)->ffi_cs_id))

size_t ctype_size(const struct cp_ctype* ct);
int cp_ctype_init();
int cp_ctype_free();
struct cp_ctype *ctype_lookup_type(char *name);
void cp_ctype_dump_stack();
void cp_error(const char *err_msg_fmt, ...);
struct cp_ctype *cp_ctype_reg_type(char *name, struct cp_ctype *ct);

void cp_push_ctype_with_name(struct cp_ctype *ct, const char *name, int nlen);
void cp_push_ctype(struct cp_ctype *ct);
void cp_set_defined(struct cp_ctype *ct);

int cp_symbol_build_func(struct cp_ctype *type,
		const char *fname, int fn_size);
int cp_symbol_build_struct(const char *stname);
int cp_symbol_build_pointer(struct cp_ctype *ct);

int ffi_cdef(const char *s);
void ffi_cparser_init(void);
void ffi_cparser_free(void);


static inline csymbol *cp_csymf_ret(csymbol_func *csf)
{
	return cp_id_to_csym(csf->ret_id);
}

static inline csymbol *cp_csymf_arg(csymbol_func *csf, int idx)
{
	return cp_id_to_csym(csf->arg_ids[idx]);
}


#else
static void __maybe_unused ffi_cparser_init(void)
{
	return;
}
static void __maybe_unused ffi_cparser_free(void)
{
	return;
}
#endif /* CONFIG_KTAP_FFI */


#endif /* __KTAP_CPARSER_H__ */
