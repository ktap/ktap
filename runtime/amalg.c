/*
 * amalg.c - ktapvm kernel module amalgamation.
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

#include "ktap.c"
#include "kp_obj.c"
#include "kp_bcread.c"
#include "kp_str.c"
#include "kp_mempool.c"
#include "kp_tab.c"
#include "kp_transport.c"
#include "kp_vm.c"
#include "kp_events.c"
#include "lib_base.c"
#include "lib_ansi.c"
#include "lib_kdebug.c"
#include "lib_timer.c"
#include "lib_table.c"
#include "lib_net.c"

