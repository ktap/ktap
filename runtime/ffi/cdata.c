/*
 * cdata.c - support functions for ktap_cdata
 *
 * This file is part of ktap by Jovi Zhangwei
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
#include "../kp_obj.h"

ktap_cdata *kp_cdata_new(ktap_state *ks, csymbol_id id)
{
	ktap_cdata *cd;

	cd = &kp_obj_newobject(ks, KTAP_TCDATA, sizeof(ktap_cdata), NULL)->cd;
	cd_set_csym_id(cd, id);

	return cd;
}

/* argument nmemb here indicates the length of array that is pointed to,
 * -1 for unknown */
ktap_cdata *kp_cdata_new_ptr(ktap_state *ks, void *addr,
				int nmemb, csymbol_id id, int to_allocate)
{
	ktap_cdata *cd;
	size_t memb_size;
	csymbol_id deref_id;

	cd = kp_cdata_new(ks, id);

	if (to_allocate) {
		/* allocate new empty space */
		/* TODO: free the space when exit the program unihorn(08.12.2013) */
		deref_id = csym_ptr_deref_id(id_to_csym(ks, id));
		memb_size = csym_size(ks, id_to_csym(ks, deref_id));
		cd_ptr(cd) = kp_zalloc(ks, memb_size * nmemb);
		cd_ptr_allocated(cd) = 1;
	} else {
		cd_ptr(cd) = addr;
		cd_ptr_allocated(cd) = 0;
	}
	cd_ptr_nmemb(cd) = nmemb;

	return cd;
}

void kp_cdata_free_ptr(ktap_state *ks, ktap_cdata *cd)
{
	if (cd_ptr_allocated(cd))
		kp_free(ks, cd_ptr(cd));
	cd_ptr(cd) = NULL;
	cd_ptr_allocated(cd) = 0;
}

ktap_cdata *kp_cdata_new_record(ktap_state *ks, void *val, csymbol_id id)
{
	ktap_cdata *cd;
	size_t size;

	cd = kp_cdata_new(ks, id);

	/* if val == NULL, allocate new empty space */
	if (val == NULL) {
		/* TODO: free the space when exit the program unihorn(08.12.2013) */
		size = csym_size(ks, id_to_csym(ks, id));
		cd_struct(cd) = kp_zalloc(ks, size);
	} else
		cd_struct(cd) = val;

	return cd;
}

void kp_cdata_dump(ktap_state *ks, ktap_cdata *cd)
{
	switch (cd_type(ks, cd)) {
	case FFI_UINT8:	case FFI_INT8:
		kp_printf(ks, "c int(0x%01x)", cd_int(cd));
		break;
	case FFI_UINT16:	case FFI_INT16:
		kp_printf(ks, "c int(0x%02x)", cd_int(cd));
		break;
	case FFI_UINT32:	case FFI_INT32:
		kp_printf(ks, "c int(0x%04x)", cd_int(cd));
		break;
	case FFI_UINT64:	case FFI_INT64:
		kp_printf(ks, "c int(0x%08x)", cd_int(cd));
		break;
	case FFI_PTR:
		kp_printf(ks, "c pointer(0x%p)", cd_ptr(cd));
		break;
	default:
		kp_printf(ks, "unsupported cdata type %d!\n", cd_type(ks, cd));
	}
}

/* Notice: Even if the types are matched, there may exist the lost of
 * data in the unpack process due to precision */
int kp_cdata_type_match(ktap_state *ks, csymbol *cs, ktap_value *val)
{
	ffi_type type;

	type = csym_type(cs);
	if (type == FFI_FUNC)
		goto error;

	switch (ttypenv(val)) {
	case KTAP_TLIGHTUSERDATA:
		if (type != FFI_PTR) goto error;
		break;
	case KTAP_TBOOLEAN:
	case KTAP_TNUMBER:
		if (type != FFI_UINT8 && type != FFI_INT8
		&& type != FFI_UINT16 && type != FFI_INT16
		&& type != FFI_UINT32 && type != FFI_INT32
		&& type != FFI_UINT64 && type != FFI_INT64)
			goto error;
		break;
	case KTAP_TSTRING:
		if (type != FFI_PTR && type != FFI_UINT8 && type != FFI_INT8)
			goto error;
		break;
	case KTAP_TCDATA:
		if (cs != cd_csym(ks, cdvalue(val)))
			goto error;
		break;
	default:
		goto error;
	}
	return 0;

error:
	return -1;
}

static void kp_cdata_value(ktap_state *ks,
		ktap_value *val, void **out_addr, size_t *out_size, void **temp)
{
	struct ktap_cdata *cd;
	csymbol *cs;
	ffi_type type;

	switch (ttypenv(val)) {
	case KTAP_TBOOLEAN:
		*out_addr = &bvalue(val);
		*out_size = sizeof(int);
		return;
	case KTAP_TLIGHTUSERDATA:
		*out_addr = pvalue(val);
		*out_size = sizeof(void *);
		return;
	case KTAP_TNUMBER:
		*out_addr = &nvalue(val);
		*out_size = sizeof(ktap_number);
		return;
	case KTAP_TSTRING:
		*temp = (void *)svalue(val);
		*out_addr = temp;
		*out_size = sizeof(void *);
		return;
	}

	cd = cdvalue(val);
	cs = cd_csym(ks, cd);
	type = csym_type(cs);
	*out_size = csym_size(ks, cs);
	switch (type) {
	case FFI_VOID:
		kp_error(ks, "Error: Cannot copy data from void type\n");
		return;
	case FFI_UINT8:
	case FFI_INT8:
	case FFI_UINT16:
	case FFI_INT16:
	case FFI_UINT32:
	case FFI_INT32:
	case FFI_UINT64:
	case FFI_INT64:
		*out_addr = &cd_int(cd);
		return;
	case FFI_PTR:
		*out_addr = &cd_ptr(cd);
		return;
	case FFI_STRUCT:
	case FFI_UNION:
		*out_addr = cd_struct(cd);
		return;
	case FFI_FUNC:
	case FFI_UNKNOWN:
		kp_error(ks, "Error: internal error for csymbol %s\n",
				csym_name(cs));
		return;
	}
}

/* Check whether or not type is matched before unpacking */
void kp_cdata_unpack(ktap_state *ks, char *dst, csymbol *cs, ktap_value *val)
{
	size_t size = csym_size(ks, cs), val_size;
	void *val_addr, *temp;

	kp_cdata_value(ks, val, &val_addr, &val_size, &temp);
	if (val_size > size)
		val_size = size;
	memmove(dst, val_addr, val_size);
	memset(dst + val_size, 0, size - val_size);
}

/* Check whether or not type is matched before packing */
void kp_cdata_pack(ktap_state *ks, ktap_value *val, char *src, csymbol *cs)
{
	size_t size = csym_size(ks, cs), val_size;
	void *val_addr, *temp;

	kp_cdata_value(ks, val, &val_addr, &val_size, &temp);
	if (size > val_size)
		size = val_size;
	memmove(val_addr, src, size);
	memset(val_addr + size, 0, val_size - size);
}

/* Init its cdata type, but not its actual value */
void kp_cdata_init(ktap_state *ks, ktap_value *val, void *addr,
		int len, csymbol_id id)
{
	ffi_type type = csym_type(id_to_csym(ks, id));

	switch (type) {
	case FFI_PTR:
		set_cdata(val, kp_cdata_new_ptr(ks, addr, len, id, 0));
		break;
	case FFI_STRUCT:
	case FFI_UNION:
		set_cdata(val, kp_cdata_new_record(ks, addr, id));
		break;
	default:
		set_cdata(val, kp_cdata_new(ks, id));
		break;
	}
}

void kp_cdata_ptr_set(ktap_state *ks, ktap_cdata *cd,
				 ktap_value *key, ktap_value *val)
{
	ktap_number idx;
	csymbol *cs;
	size_t size;
	char *addr;

	if (!is_number(key)) {
		kp_printf(ks, "array index should be number\n");
		kp_prepare_to_exit(ks);
		return;
	}
	idx = nvalue(key);
	if (unlikely(idx < 0 || (cd_ptr_nmemb(cd) >= 0
					&& idx >= cd_ptr_nmemb(cd)))) {
		kp_error(ks, "array index out of bound\n");
		return;
	}

	cs = csym_ptr_deref(ks, cd_csym(ks, cd));
	if (kp_cdata_type_match(ks, cs, val)) {
		kp_printf(ks, "array member should be %s type\n", csym_name(cs));
		kp_prepare_to_exit(ks);
		return;
	}
	size = csym_size(ks, cs);
	addr = cd_ptr(cd);
	addr += size * idx;
	kp_cdata_unpack(ks, addr, cs, val);
}

void kp_cdata_ptr_get(ktap_state *ks, ktap_cdata *cd,
				 ktap_value *key, ktap_value *val)
{
	ktap_number idx;
	csymbol *cs;
	size_t size;
	char *addr;
	csymbol_id cs_id;

	if (!is_number(key)) {
		kp_printf(ks, "array index should be number\n");
		kp_prepare_to_exit(ks);
		return;
	}
	idx = nvalue(key);
	if (unlikely(idx < 0 || (cd_ptr_nmemb(cd) >= 0
					&& idx >= cd_ptr_nmemb(cd)))) {
		kp_error(ks, "array index out of bound\n");
		return;
	}

	cs_id = csym_ptr_deref_id(cd_csym(ks, cd));
	cs = id_to_csym(ks, cs_id);
	size = csym_size(ks, cs);
	addr = cd_ptr(cd);
	addr += size * idx;

	kp_cdata_init(ks, val, addr, -1, cs_id);
	kp_cdata_pack(ks, val, addr, cs);
}

void kp_cdata_record_set(ktap_state *ks, ktap_cdata *cd,
				 ktap_value *key, ktap_value *val)
{
	const char *mb_name;
	csymbol *cs, *mb_cs;
	csymbol_struct *csst;
	struct_member *mb;
	char *addr;

	if (!is_shrstring(key)) {
		kp_printf(ks, "struct member name should be string\n");
		kp_prepare_to_exit(ks);
		return;
	}
	mb_name = svalue(key);
	cs = cd_csym(ks, cd);
	csst = csym_struct(cs);
	mb = csymst_mb_by_name(ks, csst, mb_name);
	if (mb == NULL) {
		kp_printf(ks, "struct member %s doesn't exist\n", mb_name);
		kp_prepare_to_exit(ks);
		return;
	}
	mb_cs = id_to_csym(ks, mb->id);
	if (kp_cdata_type_match(ks, mb_cs, val)) {
		kp_printf(ks, "struct member should be %s type\n", csym_name(mb_cs));
		kp_prepare_to_exit(ks);
		return;
	}
	addr = cd_struct(cd);
	addr += csym_record_mb_offset_by_name(ks, cs, mb_name);
	kp_cdata_unpack(ks, addr, mb_cs, val);
}

void kp_cdata_record_get(ktap_state *ks, ktap_cdata *cd,
				 ktap_value *key, ktap_value *val)
{
	const char *mb_name;
	csymbol *cs, *mb_cs;
	csymbol_struct *csst;
	struct_member *mb;
	char *addr;
	csymbol_id mb_cs_id;

	if (!is_shrstring(key)) {
		kp_printf(ks, "struct member name should be string\n");
		kp_prepare_to_exit(ks);
		return;
	}
	mb_name = svalue(key);
	cs = cd_csym(ks, cd);
	csst = csym_struct(cs);
	mb = csymst_mb_by_name(ks, csst, mb_name);
	if (mb == NULL) {
		kp_printf(ks, "struct member %s doesn't exist\n", mb_name);
		kp_prepare_to_exit(ks);
		return;
	}
	mb_cs_id = mb->id;
	mb_cs = id_to_csym(ks, mb_cs_id);
	addr = cd_struct(cd);
	addr += csym_record_mb_offset_by_name(ks, cs, mb_name);

	kp_cdata_init(ks, val, addr, mb->len, mb_cs_id);
	if (mb->len < 0)
		kp_cdata_pack(ks, val, addr, mb_cs);
}
