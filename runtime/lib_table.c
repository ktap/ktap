/*
 * lib_table.c - Table library
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

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_vm.h"
#include "kp_tab.h"

static int kplib_table_new(ktap_state *ks)
{
	ktap_tab *h;
	int narr = 0, nrec = 0;

	if (kp_arg_nr(ks) >= 1) {
		kp_arg_check(ks, 1, KTAP_TNUMBER);
		narr = nvalue(kp_arg(ks, 1));
	}

	if (kp_arg_nr(ks) >= 2) {
		kp_arg_check(ks, 2, KTAP_TNUMBER);
		nrec = nvalue(kp_arg(ks, 2));
	}

	h = kp_tab_new(ks, narr, nrec);
	if (!h) {
		set_nil(ks->top);
	} else {
		set_table(ks->top, h);
	}

	incr_top(ks);
	return 1;
}

static const ktap_Reg tablelib_funcs[] = {
	{"new",	kplib_table_new},
	{NULL}
};

int kp_init_tablelib(ktap_state *ks)
{
	return kp_register_lib(ks, "table", tablelib_funcs);
}

