/*
 * transport.c - relay transport functionality
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

#include <linux/debugfs.h>
#include <linux/relay.h>
#include "../include/ktap.h"

static int subbuf_start_callback(struct rchan_buf *buf, void *subbuf,
				 void *prev_subbuf, size_t prev_padding)
{
	if (!relay_buf_full(buf))
		return 1;

	return 0;
}

static int remove_buf_file_callback(struct dentry *dentry)
{
	debugfs_remove(dentry);

	return 0;
}

/*
 * we must use per-cpu relay buffer, otherwise we need to protect each
 * tracing call to order every printf call, that's really bad to performance.
 */
static struct dentry *create_buf_file_callback(const char *filename,
					       struct dentry *parent,
					       umode_t mode,
					       struct rchan_buf *buf,
					       int *is_global)
{
	return debugfs_create_file(filename, mode, parent, buf,
				   &relay_file_operations);
}

static struct rchan_callbacks relay_callbacks = {
	.subbuf_start           = subbuf_start_callback,
	.create_buf_file        = create_buf_file_callback,
	.remove_buf_file        = remove_buf_file_callback,
};

void kp_transport_write(ktap_State *ks, const void *data, size_t length)
{
	__relay_write(G(ks)->ktap_chan, data, length);
}

void *kp_transport_reserve(ktap_State *ks, size_t length)
{
	return relay_reserve(G(ks)->ktap_chan, length);
}

void kp_transport_exit(ktap_State *ks)
{
	if (G(ks)->ktap_chan)
		relay_close(G(ks)->ktap_chan);
}

extern struct dentry *ktap_dir;

int kp_transport_init(ktap_State *ks)
{
	char prefix[32] = {0};

	sprintf(prefix, "trace-%d-", (int)task_tgid_vnr(current));
	G(ks)->ktap_chan = relay_open(prefix, ktap_dir, 4096, 10,
				      &relay_callbacks, NULL);
	if (!G(ks)->ktap_chan) {
		pr_err("ktap: relay_open failed\n");
		return -1;
	}

	return 0;
}


