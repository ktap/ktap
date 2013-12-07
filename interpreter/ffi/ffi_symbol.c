/*
 * ffi_symbol.c - ktapvm kernel module ffi symbol submodule
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
#include "../kp_vm.h"
#include "../kp_obj.h"
#include "../kp_str.h"
#include "../kp_tab.h"

void setup_kp_ffi_symbol_table(ktap_state *ks);


static inline csymbol *get_csym_arr(ktap_state *ks)
{
	return G(ks)->ffis.csym_arr;
}

static inline int get_csym_nr(ktap_state *ks)
{
	return G(ks)->ffis.csym_nr;
}

static inline void set_csym_arr(ktap_state *ks, csymbol *csym)
{
	G(ks)->ffis.csym_arr = csym;
}

static inline void set_csym_nr(ktap_state *ks, int nr)
{
	G(ks)->ffis.csym_nr = nr;
}


static inline ktap_tab *get_ffi_ctable(ktap_state *ks)
{
	return G(ks)->ffis.ctable;
}

static void setup_ffi_ctable(ktap_state *ks)
{
	ktap_value ffi_lib_name, ffi_mt;
	ktap_tab *registry;
	const ktap_value *gt;

	gt = kp_tab_getint(hvalue(&G(ks)->registry), KTAP_RIDX_GLOBALS);

	G(ks)->ffis.ctable = kp_tab_new(ks);

	/* insert ffi C table to global table */
	set_table(&ffi_mt, get_ffi_ctable(ks));
	set_string(&ffi_lib_name, kp_tstring_new(ks, "C"));
	registry = hvalue(gt);
	kp_tab_setvalue(ks, registry, &ffi_lib_name, &ffi_mt);
}

void ffi_set_csym_arr(ktap_state *ks, int cs_nr, csymbol *new_arr)
{
	set_csym_nr(ks, cs_nr);
	set_csym_arr(ks, new_arr);

	if (!new_arr)
		return;

	setup_kp_ffi_symbol_table(ks);
}

inline csymbol *ffi_get_csym_by_id(ktap_state *ks, int id)
{
	return &(get_csym_arr(ks)[id]);
}

csymbol_id ffi_get_csym_id(ktap_state *ks, char *name)
{
	int i;

	for (i = 0; i < get_csym_nr(ks); i++) {
		if (!strcmp(name, csym_name(ffi_get_csym_by_id(ks, i)))) {
			return i;
		}
	}

	kp_error(ks, "Cannot find csymbol with name %s\n", name);
	return 0;
}

static void add_ffi_func_to_ctable(ktap_state *ks, csymbol_id id)
{
	ktap_value func_name, fv;
	ktap_cdata *cd;
	csymbol *cs;

	/* push cdata to ctable */
	set_cdata(&fv, kp_newobject(ks, KTAP_TCDATA, sizeof(ktap_cdata), NULL));
	cd = cdvalue(&fv);
	cd_set_csym_id(cd, id);

	cs = id_to_csym(ks, id);
	set_string(&func_name, kp_tstring_new(ks, csym_name(cs)));
	kp_tab_setvalue(ks, get_ffi_ctable(ks), &func_name, &fv);
}

void setup_kp_ffi_symbol_table(ktap_state *ks)
{
	int i;
	csymbol *cs;

	setup_ffi_ctable(ks);

	/* push all functions to ctable */
	for (i = 0; i < get_csym_nr(ks); i++) {
		cs = &get_csym_arr(ks)[i];
		switch (cs->type) {
		case FFI_FUNC:
			kp_verbose_printf(ks, "[%d] loading C function %s\n",
					i, csym_name(cs));
			add_ffi_func_to_ctable(ks, i);
			kp_verbose_printf(ks, "%s loaded\n", csym_name(cs));
			break;
		case FFI_STRUCT:
			break;
		default:
			break;
		}
	}
}

void kp_ffi_free_symbol(ktap_state *ks)
{
	int i;
	csymbol_id *arg_ids;
	csymbol *cs;

	if (!get_csym_arr(ks))
		return;

	for (i = 0; i < get_csym_nr(ks); i++) {
		cs = &get_csym_arr(ks)[i];
		switch (csym_type(cs)) {
		case FFI_FUNC:
			arg_ids = csym_func_arg_ids(cs);
			if (arg_ids)
				kp_free(ks, arg_ids);
			break;
		case FFI_STRUCT:
			/*@TODO finish this  20.11 2013 (houqp)*/
			break;
		default:
			break;
		}
	}

	kp_free(ks, get_csym_arr(ks));
}
