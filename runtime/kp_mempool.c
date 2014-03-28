/*
 * kp_mempool.c - ktap memory pool, service for string allocation
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

#include "../include/ktap_types.h"
#include "kp_obj.h"
#include "kp_str.h"

#include <linux/ctype.h>
#include <linux/module.h>
#include "ktap.h"


/*
 * allocate memory from mempool, the allocated memory will be free
 * util ktap exit.
 * TODO: lock-free allocation
 */
void *kp_mempool_alloc(ktap_state_t *ks, int size)
{
	ktap_global_state_t *g = G(ks);
	void *mempool = g->mempool;
	void *freepos = g->mp_freepos;
	void *addr;
	unsigned long flags;

	local_irq_save(flags);
	arch_spin_lock(&g->mp_lock);

	if (unlikely((unsigned long)((char *)freepos + size)) >
		     (unsigned long)((char *)mempool + g->mp_size)) {
		addr = NULL;
		goto out;
	}

	addr = freepos;
	g->mp_freepos = (char *)freepos + size;
 out:

	arch_spin_unlock(&g->mp_lock);
	local_irq_restore(flags);
	return addr;
}

/*
 * destroy mempool.
 */
void kp_mempool_destroy(ktap_state_t *ks)
{
	ktap_global_state_t *g = G(ks);

	if (!g->mempool)
		return;

	vfree(g->mempool);
	g->mempool = NULL;
	g->mp_freepos = NULL;
	g->mp_size = 0;
}

/*
 * pre-allocate size Kbytes memory pool.
 */
int kp_mempool_init(ktap_state_t *ks, int size)
{
	ktap_global_state_t *g = G(ks);

	g->mempool = vmalloc(size * 1024);
	if (!g->mempool)
		return -ENOMEM;

	g->mp_freepos = g->mempool;
	g->mp_size = size * 1024;
	g->mp_lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;
	return 0;
}

