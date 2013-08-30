/*
 * ktap.c - ktapvm kernel module main entry
 *
 * Copyright 2013 The ktap Project Developers.
 * See the COPYRIGHT file at the top-level directory of this distribution.
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
#error "Currently ktap don't support kernel older than 3.1"
#endif

#if !CONFIG_EVENT_TRACING
#error "Please enable CONFIG_EVENT_TRACING before compile ktap"
#endif

#if !CONFIG_PERF_EVENTS
#error "Please enable CONFIG_PERF_EVENTS before compile ktap"
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

static int load_trunk(struct ktap_parm *parm, unsigned long **buff)
{
	int ret;
	unsigned long *vmstart;

	vmstart = vmalloc(parm->trunk_len);
	if (!vmstart)
		return -ENOMEM;

	ret = copy_from_user(vmstart, (void __user *)parm->trunk,
			     parm->trunk_len);
	if (ret < 0) {
		vfree(vmstart);
		return -EFAULT;
	}

	*buff = vmstart;
	return 0;
}

static char **copy_argv_from_user(struct ktap_parm *parm)
{
	char **argv;
	int i, j;
	int ret;

	if (parm->argc > 1024)
		return ERR_PTR(-EINVAL);

	argv = kzalloc(parm->argc * sizeof(char *), GFP_KERNEL);
	if (!argv)
		return ERR_PTR(-ENOMEM);

	ret = copy_from_user(argv, (void __user *)parm->argv,
			     parm->argc * sizeof(char *));
	if (ret < 0) {
		kfree(argv);
		return ERR_PTR(-EFAULT);
	}

	for (i = 0; i < parm->argc; i++) {
		char __user *ustr = argv[i];
		char * kstr;
		int len;

		len = strlen_user(ustr);
		if (len > 0x1000)
			goto error;
		kstr = kmalloc(len + 1, GFP_KERNEL);
		if (!kstr)
			goto error;

		if (strncpy_from_user(kstr, ustr, len) < 0)
			goto error;

		kstr[len] = '\0';
		argv[i] = kstr;
	}

	return argv;
 error:
	for (j = 0; j <= i; j++)
		kfree(argv[j]);

	kfree(argv);
	return ERR_PTR(-ENOMEM);
}

static void free_argv(int argc, char **argv)
{
	int i;

	for (i = 0; i < argc; i++)
		kfree(argv[i]);

	kfree(argv);
}

static atomic_t ktap_running = ATOMIC_INIT(0);

/* Ktap Main Entry */
static int ktap_main(struct file *file, struct ktap_parm *parm)
{
	unsigned long *buff = NULL;
	ktap_state *ks;
	ktap_closure *cl;
	char **argv;
	int ret;

	if (atomic_inc_return(&ktap_running) != 1) {
		atomic_dec(&ktap_running);
		pr_info("only one ktap thread allow to run\n");
		return -EBUSY;
	}

	ret = load_trunk(parm, &buff);
	if (ret) {
		pr_err("cannot load file\n");
		goto out;
	}

	argv = copy_argv_from_user(parm);
	if (IS_ERR(argv)) {
		vfree(buff);
		ret = PTR_ERR(argv);
		goto out;
	}

	ks = kp_newstate(parm, argv);

	/* free argv memory after store into arg table */
	free_argv(parm->argc, argv);

	if (unlikely(!ks)) {
		vfree(buff);
		ret = -ENOEXEC;
		goto out;
	}

	file->private_data = ks;

	cl = kp_load(ks, (unsigned char *)buff);

	vfree(buff);

	if (cl) {
		/* optimize bytecode before excuting */
		kp_optimize_code(ks, 0, cl->l.p);
		kp_call(ks, ks->top - 1, 0);
	}

	kp_final_exit(ks);

 out:
	atomic_dec(&ktap_running);	
	return ret;
}


static void print_version(void)
{
}

static long ktap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ktap_parm parm;
	int ret;

	switch (cmd) {
	case KTAP_CMD_IOC_VERSION:
		print_version();
		return 0;
	case KTAP_CMD_IOC_RUN:
		ret = copy_from_user(&parm, (void __user *)arg,
				     sizeof(struct ktap_parm));
		if (ret < 0)
			return -EFAULT;

		return ktap_main(file, &parm);
	default:
		return -EINVAL;
	};

        return 0;
}

static const struct file_operations ktap_fops = {
	.llseek                 = no_llseek,
	.unlocked_ioctl         = ktap_ioctl,
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

	file->private_data = NULL;
	fd_install(new_fd, new_file);
	return new_fd;
}

static const struct file_operations ktapvm_fops = {
	.owner  = THIS_MODULE,
	.unlocked_ioctl         = ktapvm_ioctl,
};

struct dentry *ktap_dir;
unsigned int kp_stub_exit_instr;

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

	SET_OPCODE(kp_stub_exit_instr, OP_EXIT);

	return 0;
}

static void __exit exit_ktap(void)
{
	debugfs_remove_recursive(ktap_dir);
}

module_init(init_ktap);
module_exit(exit_ktap);

MODULE_AUTHOR("zhangwei(Jovi) <jovi.zhangwei@gmail.com>");
MODULE_DESCRIPTION("ktap");
MODULE_LICENSE("GPL");

