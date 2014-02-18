/*
 * lib_base.c - base library
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

#include <net/inet_sock.h>
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_vm.h"

/**
 * Return the source IP address for a given sock
 */
static int kplib_ip_sock_saddr(ktap_state *ks)
{
	struct inet_sock *isk;
	int family;

	kp_arg_check(ks, 1, KTAP_TYPE_NUMBER);

	/* need to validate the address firstly */	
	isk = (struct inet_sock *)nvalue(kp_arg(ks, 1));
	family = isk->sk.__sk_common.skc_family;

	if (family == AF_INET) {
		set_number(ks->top, isk->inet_rcv_saddr);
	} else {
		kp_error(ks, "ip_sock_saddr only support ipv4 now\n");
		set_nil(ks->top);
	}

	incr_top(ks);
	return 1;
}

/**
 * Return the destination IP address for a given sock
 */
static int kplib_ip_sock_daddr(ktap_state *ks)
{
	struct inet_sock *isk;
	int family;

	kp_arg_check(ks, 1, KTAP_TYPE_NUMBER);

	/* need to validate the address firstly */	
	isk = (struct inet_sock *)nvalue(kp_arg(ks, 1));
	family = isk->sk.__sk_common.skc_family;

	if (family == AF_INET) {
		set_number(ks->top, isk->inet_daddr);
	} else {
		kp_error(ks, "ip_sock_daddr only support ipv4 now\n");
		set_nil(ks->top);
	}

	incr_top(ks);
	return 1;

}

/**
 * Returns a string representation for an IP address
 */
static int kplib_format_ip_addr(ktap_state *ks)
{
	char ipstr[32];
	__be32 ip;

	kp_arg_check(ks, 1, KTAP_TYPE_NUMBER);
	ip = (__be32)nvalue(kp_arg(ks, 1));
	
	snprintf(ipstr, 32, "%pI4", &ip);
	set_string(ks->top, kp_str_new(ks, ipstr));
	incr_top(ks);
	return 1;
}

static const ktap_Reg net_funcs[] = {
	{"ip_sock_saddr", kplib_ip_sock_saddr},
	{"ip_sock_daddr", kplib_ip_sock_daddr},
	{"format_ip_addr", kplib_format_ip_addr},
	{NULL}
};

int kp_init_netlib(ktap_state *ks)
{
	return kp_register_lib(ks, "net", net_funcs); 
}
