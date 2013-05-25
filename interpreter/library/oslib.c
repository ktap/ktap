/*
 * oslib.c - Linux basic library function support for ktap
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

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include "../../include/ktap.h"

/*
 * todo: make more robust for this function
 * make this buffer percpu
 *
 * Note:
 * this function cannot be use in net console like ssh env
 * otherwise the result is not correct, and possible crash
 * you terminal
 * Use this function in raw pts terminal
 */
void kp_printf(ktap_State *ks, const char *fmt, ...)
{
	char buff[512];
	va_list args;
	int len;

	va_start(args, fmt);
	len = vscnprintf(buff, 512, fmt, args);
	va_end(args);

	kp_transport_write(ks, buff, len);
}


/* get argument operation macro */
#define GetArg(ks, n)	((ks)->ci->func + (n))
#define GetArgN(ks)	((int)(ks->top - (ks->ci->func + 1)))

static int ktap_lib_clock(ktap_State *ks)
{
	kp_printf(ks, "ktap_clock\n");
	return 0;
}

static int ktap_lib_info(ktap_State *ks)
{
	return 0;
}

static int ktap_lib_sleep(ktap_State *ks)
{
	Tvalue *time = GetArg(ks, 1);

	/* only mainthread can sleep
	if (ks != G(ks)->mainthread)
		return 0;
	*/

	msleep_interruptible(nvalue(time));

	if (fatal_signal_pending(current))
		kp_exit(ks);
	return 0;
}

/* wait forever unit interrupt by signal */
static int ktap_lib_wait(ktap_State *ks)
{
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();
	if (fatal_signal_pending(current))
		kp_exit(ks);

	return 0;
}

static const ktap_Reg oslib_funcs[] = {
	{"clock", ktap_lib_clock},
	{"info", ktap_lib_info},
	{"sleep", ktap_lib_sleep},
	{"wait", ktap_lib_wait},
	{NULL}
};

void kp_init_oslib(ktap_State *ks)
{
	kp_register_lib(ks, "os", oslib_funcs);
}

