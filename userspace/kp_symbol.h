/*
 * symbol.h - extract symbols from DSO.
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2013 Azat Khuzhin <a3at.mail@gmail.com>.
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


#define FIND_SYMBOL 1
#define FIND_STAPSDT_NOTE 2

#ifndef NO_LIBELF

#include <gelf.h>
#include <sys/queue.h>

typedef GElf_Addr vaddr_t;
typedef int (*symbol_actor)(const char *name, vaddr_t addr, void *arg);

/**
 * Parse all DSO symbols/sdt notes and all for every of them
 * an actor.
 *
 * @exec - path to DSO
 * @type - see FIND_*
 * @symbol_actor - actor to call (callback)
 * @arg - argument for @actor
 *
 * @return
 * If there have errors, return negative value;
 * No symbols found, return 0;
 * Otherwise return number of dso symbols found
 */
int
parse_dso_symbols(const char *exec, int type, symbol_actor actor, void *arg);
#endif
