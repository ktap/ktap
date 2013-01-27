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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/relay.h>

#include "ktap.h"


static struct rchan *ktap_chan;
static struct dentry *ktap_dir;


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

void ktap_transport_write(const void *data, size_t length)
{
	relay_write(ktap_chan, data, length);
}

void *ktap_transport_reserve(size_t length)
{
	return relay_reserve(ktap_chan, length);
}

void ktap_transport_exit()
{
	if (ktap_chan)
		relay_close(ktap_chan);
	if (ktap_dir)
		debugfs_remove(ktap_dir);
}

int ktap_transport_init()
{
	ktap_dir = debugfs_create_dir("ktap", NULL);
	if (!ktap_dir) {
		pr_err("ktap: debugfs_create_dir failed\n");
		return -1;
	}

	ktap_chan = relay_open("trace", ktap_dir, 1024, 1, &relay_callbacks, NULL);
	if (!ktap_chan) {
		pr_err("ktap: relay_open failed\n");
		debugfs_remove(ktap_dir);
		return -1;
	}

	return 0;
}


