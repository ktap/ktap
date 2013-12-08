/*
 * ffi.c - ktapvm kernel module ffi library
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

#include "../include/ktap_types.h"
#include "../include/ktap_ffi.h"
#include "ktap.h"
#include "kp_vm.h"

/*@TODO Design how to implement ffi helper functions  22.11 2013 (unihorn)*/

static int kp_ffi_new(ktap_state *ks)
{
	/*@TODO finish this  08.11 2013 (houqp)*/
	return 0;
}

static int kp_ffi_sizeof(ktap_state *ks)
{
	/*@TODO finish this  08.11 2013 (houqp)*/
	return 0;
}

static const ktap_Reg ffi_funcs[] = {
	{"sizeof", kp_ffi_sizeof},
	{"new", kp_ffi_new},
	{NULL}
};

void kp_init_ffilib(ktap_state *ks)
{
	kp_register_lib(ks, "ffi", ffi_funcs);
}
