/*
 * baselib.c - ktapvm kernel module base library
 *
 * Copyright (C) 2012-2013 Jovi Zhang
 *
 * Author: Jovi Zhang <bookjovi@gmail.com>
 *         zhangwei(Jovi) <jovi.zhangwei@huawei.com>
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

#include <linux/module.h>
#include <linux/slab.h>

#include "ktap.h"

/* get argument operation macro */
#define GetArg(ks, n)	((ks)->ci->func + (n))
#define GetArgN(ks)	((int)(ks->top - (ks->ci->func + 1)))

/*
 * There have some ktap_printf calling in print, so
 * we need to protect race again each thread, to make output
 * more alignment when multithread call print at same time
 *
 * todo: move this mutex to G(ks)
 */
static DEFINE_MUTEX(ktap_print_mutex);

static int ktap_lib_print(ktap_State *ks)
{
	int i;
	int n = GetArgN(ks);

	mutex_lock(&ktap_print_mutex);
	for (i = 1; i <= n; i++) {
		Tvalue *arg = GetArg(ks, i);
		if (i > 1)
			ktap_printf(ks, "\t");
		showobj(ks, arg);
	}

	ktap_printf(ks, "\n");
	mutex_unlock(&ktap_print_mutex);
	return 0;
}


/* todo: how to invoke printf in ktap? */
static int ktap_lib_printf(ktap_State *ks)
{
	return 0;
}

static int ktap_lib_printk(ktap_State *ks)
{
	return 0;
}

static const ktap_Reg base_funcs[] = {
//  {"assert", ktap_assert},
//  {"collectgarbage", ktap_collectgarbage},
//  {"error", ktap_error},
//  {"getmetatable", ktap_getmetatable},
//  {"setmetatable", ktap_setmetatable},
//  {"ipairs", ktap_ipairs},
//  {"next", ktap_next},
//  {"pairs", ktap_pairs},
	{"print", ktap_lib_print},
	{"printf", ktap_lib_printf},
	{"printk", ktap_lib_printk},
//  {"tonumber", ktap_tonumber},
//  {"tostring", ktap_tostring},
//  {"type", ktap_type},
  {NULL}
};

void ktap_init_baselib(ktap_State *ks)
{
	ktap_register_lib(ks, NULL, base_funcs); 
}
