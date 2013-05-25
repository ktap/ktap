/*
 * ktap.c - ktapvm kernel module main entry
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

/*
 * this file is the first file to be compile, add CONFIG_ checking in here.
 * See Requirements in doc/introduction.txt
 */

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
#error "Currently ktap don't support kernel older than 3.1, if you really "
       "want to use ktap in your older kernel, please contact ktap author"
#endif

#if !CONFIG_EVENT_TRACING
#error "Please enable CONFIG_EVENT_TRACING before compile ktap"
#endif

#if !CONFIG_PERF_EVENTS
#error "Please enable CONFIG_PERF_EVENTS before compile ktap"
#endif

#if !CONFIG_KPROBES
#error "Please enable CONFIG_KPROBES before compile ktap"
#endif

#if !CONFIG_KALLSYMS_ALL
#error "Please enable CONFIG_KALLSYMS before compile ktap"
#endif


#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/anon_inodes.h>
#include <linux/debugfs.h>
#include <linux/vmalloc.h>
#include "../include/ktap.h"

static int load_trunk(struct ktap_user_parm *uparm_ptr, unsigned long **buff)
{
	int ret;
	unsigned long *vmstart;

	vmstart = vmalloc(uparm_ptr->trunk_len);
	if (!vmstart)
		return -ENOMEM;

	ret = copy_from_user(vmstart, (void __user *)uparm_ptr->trunk,
			     uparm_ptr->trunk_len);
	if (ret < 0) {
		vfree(vmstart);
		return -EFAULT;
	}

	*buff = vmstart;
	return 0;
}

/* Ktap Main Entry */
static int ktap_main(struct file *file, struct ktap_user_parm *uparm_ptr)
{
	unsigned long *buff = NULL;
	ktap_State *ks;
	Closure *cl;
	int argc;
	char **argv, *argstr;
	int ret;

	argstr = kmalloc(uparm_ptr->arglen, GFP_KERNEL);
	if (!argstr)
		return -ENOMEM;

	ret = copy_from_user(argstr, (void __user *)uparm_ptr->argstr,
			     uparm_ptr->arglen);
	if (ret < 0) {
		kfree(argstr);
		return -EFAULT;
	}

	argv = argv_split(GFP_KERNEL, argstr, &argc);
	if (!argv) {
		kfree(argstr);
		pr_err("out of memory");
		return -ENOMEM;
	}

	kfree(argstr);

	ret = load_trunk(uparm_ptr, &buff);
	if (ret) {
		pr_err("cannot load file %s\n", argv[0]);
		argv_free(argv);
		return ret;
	}

	ks = kp_newstate((ktap_State **)&file->private_data, argc, argv);

	argv_free(argv);
	if (unlikely(!ks)) {
		vfree(buff);
		return -ENOEXEC;
	}

	cl = kp_load(ks, (unsigned char *)buff);

	vfree(buff);

	if (cl) {
		/* optimize bytecode before excuting */
		kp_optimize_code(ks, 0, cl->l.p);
		kp_call(ks, ks->top - 1, 0);
	}

	kp_exit(ks);
	return 0;
}


static void print_version(void)
{
}

static long ktap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ktap_user_parm uparm;
	int ret;

	switch (cmd) {
	case KTAP_CMD_VERSION:
		print_version();
		return 0;
	case KTAP_CMD_RUN:
		ret = copy_from_user(&uparm, (void __user *)arg,
				     sizeof(struct ktap_user_parm));
		if (ret < 0)
			return -EFAULT;

		return ktap_main(file, &uparm);
	case KTAP_CMD_USER_COMPLETE: {
		ktap_State *ks = file->private_data;
		kp_user_complete(ks);
		break;
		}
	default:
		return -EINVAL;
	};

        return 0;
}

static unsigned int ktap_poll(struct file *file, poll_table *wait)
{
	ktap_State *ks = file->private_data;

	if (!ks)
		return 0;

	if (G(ks)->user_completion)
		return POLLERR;

	return 0;
}

static const struct file_operations ktap_fops = {
	.llseek                 = no_llseek,
	.unlocked_ioctl         = ktap_ioctl,
	.poll			= ktap_poll
};



static long ktapvm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int new_fd, err;
	struct file *new_file;

	new_fd = get_unused_fd();
	if (new_fd < 0)
		return new_fd;

	new_file = anon_inode_getfile("[ktap]", &ktap_fops, NULL, O_RDWR);
	if (IS_ERR(new_file)) {
		err = PTR_ERR(new_file);
		put_unused_fd(new_fd);
		return err;
	}

	file->private_data = 0;
	fd_install(new_fd, new_file);
	return new_fd;
}

static const struct file_operations ktapvm_fops = {
	.owner  = THIS_MODULE,
	.unlocked_ioctl         = ktapvm_ioctl,
};

struct dentry *ktap_dir;

static int __init init_ktap(void)
{
	struct dentry *ktapvm_dentry;

	ktap_dir = debugfs_create_dir("ktap", NULL);
	if (!ktap_dir) {
		pr_err("ktap: debugfs_create_dir failed\n");
		return -1;
	}

	ktapvm_dentry = debugfs_create_file("ktapvm", 0444, ktap_dir, NULL,
					    &ktapvm_fops);

	if (!ktapvm_dentry) {
		pr_err("ktapvm: cannot create ktapvm file\n");
		debugfs_remove_recursive(ktap_dir);
		return -1;
	}

	return 0;
}

static void __exit exit_ktap(void)
{
	debugfs_remove_recursive(ktap_dir);
}


module_init(init_ktap);
module_exit(exit_ktap);

MODULE_AUTHOR("Jovi Zhang <bookjovi@gmail.com>");
MODULE_DESCRIPTION("ktap");
MODULE_LICENSE("GPL");

