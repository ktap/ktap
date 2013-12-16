/*
 * ffi_util.c - utility function for ffi module
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
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


#include "../../include/ktap_types.h"
#include "../../include/ktap_ffi.h"
#include "../ktap.h"

static void init_csym_struct(ktap_state *ks, csymbol_struct *csst)
{
	int nr = csymst_mb_nr(csst);
	size_t size = 0;
	size_t align = 1;
	int i, var_len;
	size_t var_size, var_align;

	for (i = 0; i < nr; i++) {
		csymbol *var_cs = csymst_mb_csym(ks, csst, i);
		var_len = csymst_mb_len(csst, i);
		if (var_len < 0) {
			var_len = 1;
		} else {
			var_cs = csym_ptr_deref(ks, var_cs);
		}
		var_size = csym_size(ks, var_cs) * var_len;
		var_align = csym_align(ks, var_cs);
		size = ALIGN(size, var_align);
		size += var_size;
		align = align > var_align ? align : var_align;
	}
	size = ALIGN(size, align);
	csst->size = size;
	csst->align = align;
}

static void init_csym_union(ktap_state *ks, csymbol_struct *csst)
{
	int nr = csymst_mb_nr(csst);
	size_t size = 0;
	size_t align = 1;
	int i, var_len;
	size_t var_size, var_align;

	for (i = 0; i < nr; i++) {
		csymbol *var_cs = csymst_mb_csym(ks, csst, i);
		var_len = csymst_mb_len(csst, i);
		if (var_len < 0) {
			var_len = 1;
		} else {
			var_cs = csym_ptr_deref(ks, var_cs);
		}
		var_size = csym_size(ks, var_cs) * var_len;
		var_align = csym_align(ks, var_cs);
		size = size > var_size ? size : var_size;
		align = align > var_align ? align : var_align;
	}
	csst->size = size;
	csst->align = align;
}


size_t csym_size(ktap_state *ks, csymbol *cs)
{
	ffi_type type = csym_type(cs);
	switch(type) {
	case FFI_STRUCT:
		if (csym_struct(cs)->align == 0)
			init_csym_struct(ks, csym_struct(cs));
		return csym_struct(cs)->size;
	case FFI_UNION:
		if (csym_struct(cs)->align == 0)
			init_csym_union(ks, csym_struct(cs));
		return csym_struct(cs)->size;
	default:
		return ffi_type_size(type);
	}
}

size_t csym_align(ktap_state *ks, csymbol *cs)
{
	ffi_type type = csym_type(cs);
	switch(type) {
	case FFI_STRUCT:
		if (csym_struct(cs)->align == 0)
			init_csym_struct(ks, csym_struct(cs));
		return csym_struct(cs)->align;
	case FFI_UNION:
		if (csym_struct(cs)->align == 0)
			init_csym_union(ks, csym_struct(cs));
		return csym_struct(cs)->align;
	default:
		return ffi_type_align(type);
	}
}

size_t csym_record_mb_offset(ktap_state *ks, csymbol *cs, int idx)
{
	csymbol_struct *csst = csym_struct(cs);
	int nr = csymst_mb_nr(csst);
	size_t off = 0;
	size_t align = 1;
	int i, var_len;
	size_t var_size, var_align;

	if (idx < 0 || idx > nr)
		return -1;

	if (csym_type(cs) == FFI_UNION)
		return 0;

	for (i = 0; i <= idx; i++) {
		csymbol *var_cs = csymst_mb_csym(ks, csst, i);
		var_len = csymst_mb_len(csst, i);
		if (var_len < 0) {
			var_len = 1;
		} else {
			var_cs = csym_ptr_deref(ks, var_cs);
		}
		var_size = csym_size(ks, var_cs) * var_len;
		var_align = csym_align(ks, var_cs);
		off = ALIGN(off, var_align);
		if (i == idx)
			break;
		off += var_size;
		align = align > var_align ? align : var_align;
	}
	return off;
}

int csymst_mb_idx_by_name(ktap_state *ks,
		csymbol_struct *csst, const char *name)
{
	int nr = csymst_mb_nr(csst);
	int i;

	for (i = 0; i < nr; i++)
		if (!strcmp(name, csymst_mb_name(csst, i)))
			return i;
	return -1;
}
