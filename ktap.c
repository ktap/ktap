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


#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/anon_inodes.h>
#include <linux/debugfs.h>
#include "ktap.h"

static void remove_file(struct file *file, const char *filename)
{
	int err;
	struct path parent;

	err = kern_path(filename, LOOKUP_PARENT, &parent);
	if (err)
                return;

	dget(file->f_path.dentry);
	err = vfs_unlink(parent.dentry->d_inode, file->f_path.dentry);
	if (err && err != -ENOENT)
		pr_warn("cannot unlink file %s\n", filename);

	path_put(&parent);
}

static void need_remove_ktapc_out(struct file *file, const char *path)
{
	int pid, ret;

	ret = sscanf(path, TEMP_KTAPC_OUT_PATH_FMT, &pid);
	if ((ret > 0) && (pid == task_tgid_vnr(current))) {
		/* remove ktapc.out file in /tmp */
		remove_file(file, path);
	}
}

static int loadfile(const char *path, unsigned long **buff)
{
	struct file *file;
	struct kstat st;
	int ret;
	unsigned long *vmstart = NULL;

	file = filp_open(path, O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (!S_ISREG(file->f_path.dentry->d_inode->i_mode)) {
		ret = -EACCES;
		goto out;
	}

	ret = vfs_getattr(&file->f_path, &st);
	if (ret)
		goto out;

	if (st.size == 0) {
		ret = -EINVAL;
		goto out;
	}
		
	vmstart = vmalloc(st.size);
	if (!vmstart) {
		ret = -ENOMEM;
		goto out;
	}

	ret = kernel_read(file, 0, (char *)vmstart, st.size);
	if (ret != st.size) {
		vfree(vmstart);
		goto out;
	}

	ret = 0;

 out:
	*buff = vmstart;

	/*
	 * remove /tmp/ktapc.out.$pid file before interpreter start
	 * This will avoid file leak in /tmp if ktapvm crashed.
	 */
	need_remove_ktapc_out(file, path);

	filp_close(file, NULL);
	return ret;
}

/* Ktap Main Entry */
static int ktap_main(struct file *file, char *cmdline)
{
	unsigned long *buff = NULL;
	ktap_State *ks;
	Closure *cl;
	int argc;
	char **argv;
	int ret;

	argv = argv_split(GFP_KERNEL, cmdline, &argc);
	if (!argv) {
		pr_err("out of memory");
		return -EINVAL;
	}

	ret = loadfile(argv[0], &buff);
	if (ret) {
		pr_err("cannot load file %s\n", argv[0]);
		argv_free(argv);
		return ret;
	}

	ks = ktap_newstate((ktap_State **)&file->private_data, argc, argv);

	argv_free(argv);
	if (unlikely(!ks)) {
		vfree(buff);
		return -ENOEXEC;
	}

	cl = ktap_load(ks, (unsigned char *)buff);

	vfree(buff);

	if (cl) {
		/* optimize bytecode before excuting */
		ktap_optimize_code(ks, 0, cl->l.p);
		ktap_call(ks, ks->top - 1, 0);
	}

	ktap_exit(ks);
	return 0;
}


static void print_version(void)
{
}

static long ktap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	char cmdline[64] = {0};
	int ret;

	switch (cmd) {
	case KTAP_CMD_VERSION:
		print_version();
		return 0;
	case KTAP_CMD_RUN:
		ret = copy_from_user(cmdline, (void __user *)arg, 64);
		if (ret < 0)
			return -EFAULT;

		ktap_main(file, cmdline);
		break;
	case KTAP_CMD_USER_COMPLETE: {
		ktap_State *ks = file->private_data;
		ktap_user_complete(ks);
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

