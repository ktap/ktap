/*
 * lib_base.c - base library
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

#include <net/inet_sock.h>
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_obj.h"
#include "kp_str.h"
#include "kp_vm.h"

/**
 * Return the source IP address for a given sock
 */
static int kplib_net_ip_sock_saddr(ktap_state_t *ks)
{
	struct inet_sock *isk;
	int family;

	/* TODO: need to validate the address firstly */	

	isk = (struct inet_sock *)kp_arg_checknumber(ks, 1);
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
static int kplib_net_ip_sock_daddr(ktap_state_t *ks)
{
	struct inet_sock *isk;
	int family;

	/* TODO: need to validate the address firstly */	

	isk = (struct inet_sock *)kp_arg_checknumber(ks, 1);
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
static int kplib_net_format_ip_addr(ktap_state_t *ks)
{
	__be32 ip = (__be32)kp_arg_checknumber(ks, 1);
	ktap_str_t *ts;
	char ipstr[32];

	snprintf(ipstr, 32, "%pI4", &ip);
	ts = kp_str_newz(ks, ipstr);
	if (ts) {
		set_string(ks->top, kp_str_newz(ks, ipstr));
		incr_top(ks);
		return 1;
	} else
		return -1;
}

static const ktap_libfunc_t net_lib_funcs[] = {
	{"ip_sock_saddr", kplib_net_ip_sock_saddr},
	{"ip_sock_daddr", kplib_net_ip_sock_daddr},
	{"format_ip_addr", kplib_net_format_ip_addr},
	{NULL}
};

int kp_lib_init_net(ktap_state_t *ks)
{
	return kp_vm_register_lib(ks, "net", net_lib_funcs); 
}
