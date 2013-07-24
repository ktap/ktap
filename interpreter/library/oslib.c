/*
 * oslib.c - Linux basic library function support for ktap
 *
 * Copyright 2013 The ktap Project Developers.
 * See the COPYRIGHT file at the top-level directory of this distribution.
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

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include "../../include/ktap.h"

static int ktap_lib_sleep(ktap_state *ks)
{
	ktap_value *time = kp_arg(ks, 1);

	/* only mainthread can sleep */
	if (ks != G(ks)->mainthread)
		return 0;

	msleep_interruptible(nvalue(time));

	if (fatal_signal_pending(current))
		kp_exit(ks);
	return 0;
}

static const ktap_Reg oslib_funcs[] = {
	{"sleep", ktap_lib_sleep},
	{NULL}
};

void kp_init_oslib(ktap_state *ks)
{
	kp_register_lib(ks, "os", oslib_funcs);
}

