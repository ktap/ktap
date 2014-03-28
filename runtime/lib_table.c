/*
 * lib_table.c - Table library
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2014 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
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

static int kplib_table_new(ktap_state_t *ks)
{
	int narr = kp_arg_checkoptnumber(ks, 1, 0);
	int nrec = kp_arg_checkoptnumber(ks, 2, 0);
	ktap_tab_t *h;

	h = kp_tab_new_ah(ks, narr, nrec);
	if (!h) {
		set_nil(ks->top);
	} else {
		set_table(ks->top, h);
	}

	incr_top(ks);
	return 1;
}

static const ktap_libfunc_t table_lib_funcs[] = {
	{"new",	kplib_table_new},
	{NULL}
};

int kp_lib_init_table(ktap_state_t *ks)
{
	return kp_vm_register_lib(ks, "table", table_lib_funcs);
}

