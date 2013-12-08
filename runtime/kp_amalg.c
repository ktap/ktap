/*
 * kp_amalg.c - ktapvm kernel module amalgamation.
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


#include "ktap.c"
#include "kp_opcode.c"
#include "kp_obj.c"
#include "kp_load.c"
#include "kp_str.c"
#include "kp_tab.c"
#include "kp_transport.c"
#include "kp_vm.c"
#include "lib_base.c"
#include "lib_ansi.c"
#include "lib_kdebug.c"
#include "lib_timer.c"

#ifdef CONFIG_KTAP_FFI
#include "ffi/ffi_call.c"
#include "ffi/ffi_type.c"
#include "ffi/ffi_symbol.c"
#include "ffi/cdata.c"
#include "ffi/ffi_util.c"
#include "lib_ffi.c"
#endif
