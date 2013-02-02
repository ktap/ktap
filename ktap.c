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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/user.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/anon_inodes.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/debugfs.h>

#include "ktap.h"

static int loadfile(const char *path, unsigned long **buff)
{
	struct file *file;
	struct kstat st;
	int ret;
	unsigned long *vmstart;

	file = filp_open(path, O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (!S_ISREG(file->f_path.dentry->d_inode->i_mode)) {
		filp_close(file, NULL);
		return -EACCES;
	}

	ret = vfs_getattr(file->f_path.mnt, file->f_path.dentry, &st);
	if (ret) {
		filp_close(file, NULL);
		return ret;
	}

	if (st.size == 0) {
		filp_close(file, NULL);
		return -EINVAL;
	}
		
	vmstart = vmalloc(st.size);
	if (!vmstart) {
		filp_close(file, NULL);
		return -ENOMEM;
	}

	ret = kernel_read(file, 0, (char *)vmstart, st.size);
	if (ret != st.size) {
		vfree(vmstart);
		filp_close(file, NULL);
		return ret;
	}

	filp_close(file, NULL);

//	print_hex_dump(KERN_INFO, "ktapvm: ", DUMP_PREFIX_OFFSET, 16, 1,
//		       vmstart, filesize, true);

	*buff = vmstart;

	return 0;
}

/* Ktap Main Entry */
static int ktap_main(int argc, char **argv)
{
	unsigned long *buff = NULL;
	ktap_State *ks;
	Closure *cl;
	int ret;

//	if (parse_option(argc, argv, ks) < 0)
//		print_usage();

	ret = loadfile(argv[0], &buff);
	if (unlikely(ret))
		return ret;

	ks = ktap_newstate();
	if (unlikely(!ks))
		return -ENOEXEC;

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
	int argc, ret;
	char **argv;

	if (cmd == KTAP_CMD_VERSION) {
		print_version();
		return 0;
	}

	if (cmd != KTAP_CMD_RUN)
		return -EINVAL;

	ret = copy_from_user(cmdline, (void __user *)arg, 64);
	if (ret < 0)
		return -EFAULT;

	argv = argv_split(GFP_KERNEL, cmdline, &argc);
	if (!argv) {
		pr_err("out of memory");
		return -EINVAL;
	}

	ktap_main(argc, argv);

	argv_free(argv);
        return 0;
}

static const struct file_operations ktap_fops = {
	.llseek                 = no_llseek,
	.unlocked_ioctl         = ktap_ioctl,
	.compat_ioctl           = ktap_ioctl,
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

	fd_install(new_fd, new_file);
	return new_fd;
}

static const struct file_operations ktapvm_fops = {
	.owner  = THIS_MODULE,
	.unlocked_ioctl         = ktapvm_ioctl,
};

static struct miscdevice ktap_miscdev = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = "ktapvm",
        .fops = &ktapvm_fops,
};


struct dentry *ktap_dir;

static int __init init_ktap(void)
{
        int ret;

        ret = misc_register(&ktap_miscdev);
        if (ret < 0) {
                pr_err("Can't register ktap misc device");
		return ret;
	}

	ktap_dir = debugfs_create_dir("ktap", NULL);
	if (!ktap_dir) {
		ktap_dir = debugfs_get_dentry("ktap");
		if (!ktap_dir) {
			pr_err("ktap: debugfs_create_dir failed\n");
			goto err;
		}
	}

	return 0;

 err:
	misc_deregister(&ktap_miscdev);
        return  -1;
}

static void __exit exit_ktap(void)
{
        misc_deregister(&ktap_miscdev);
	debugfs_remove(ktap_dir);
}


module_init(init_ktap);
module_exit(exit_ktap);

MODULE_AUTHOR("Jovi Zhang <bookjovi@gmail.com>");
MODULE_DESCRIPTION("ktap");
MODULE_LICENSE("GPL");

