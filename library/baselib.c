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
#include <linux/hardirq.h>
#include <linux/slab.h>

#include "../ktap.h"

/* get argument operation macro */
#define GetArg(ks, n)	((ks)->ci->func + (n))
#define GetArgN(ks)	((int)(ks->top - (ks->ci->func + 1)))

static int ktap_lib_print(ktap_State *ks)
{
	int i;
	int n = GetArgN(ks);

	for (i = 1; i <= n; i++) {
		Tvalue *arg = GetArg(ks, i);
		if (i > 1)
			ktap_printf(ks, "\t");
		showobj(ks, arg);
	}

	ktap_printf(ks, "\n");

	return 0;
}

/* don't engage with tstring when printf, use buffer directly */
static int ktap_lib_printf(ktap_State *ks)
{
	struct trace_seq seq;

	trace_seq_init(&seq);
	if (ktap_strfmt(ks, &seq)) {
		return 0;
	}

	seq.buffer[seq.len] = '\0';
	ktap_transport_write(ks, seq.buffer, seq.len);

	return 0;
}

static int ktap_lib_in_interrupt(ktap_State *ks)
{
	int ret = in_interrupt();

	setnvalue(ks->top, ret);
	incr_top(ks);
	return 1;
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
	{"in_interrupt", ktap_lib_in_interrupt},
//  {"tonumber", ktap_tonumber},
//  {"tostring", ktap_tostring},
//  {"type", ktap_type},
  {NULL}
};

void ktap_init_baselib(ktap_State *ks)
{
	ktap_register_lib(ks, NULL, base_funcs); 
}
