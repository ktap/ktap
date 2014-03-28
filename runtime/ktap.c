/*
 * ktap.c - ktapvm kernel module main entry
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

/*
 * this file is the first file to be compile, add CONFIG_ checking in here.
 * See Requirements in doc/tutorial.md
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

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/anon_inodes.h>
#include <linux/debugfs.h>
#include <linux/vmalloc.h>
#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_bcread.h"
#include "kp_vm.h"

/* common helper function */
long gettimeofday_ns(void)
{
	struct timespec now;

	getnstimeofday(&now);
	return now.tv_sec * NSEC_PER_SEC + now.tv_nsec;
}

static int load_trunk(ktap_option_t *parm, unsigned long **buff)
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

static struct dentry *kp_dir_dentry;

/* Ktap Main Entry */
static int ktap_main(struct file *file, ktap_option_t *parm)
{
	unsigned long *buff = NULL;
	ktap_state_t *ks;
	ktap_proto_t *pt;
	long start_time, delta_time;
	int ret;

	start_time = gettimeofday_ns();

	ks = kp_vm_new_state(parm, kp_dir_dentry);
	if (unlikely(!ks))
		return -ENOEXEC;

	file->private_data = ks;

	ret = load_trunk(parm, &buff);
	if (ret) {
		kp_error(ks, "cannot load file\n");
		goto out;
	}

	pt = kp_bcread(ks, (unsigned char *)buff, parm->trunk_len);

	vfree(buff);

	if (pt) {
		/* validate byte code */
		if (kp_vm_validate_code(ks, pt, ks->stack))
			goto out;

		delta_time = (gettimeofday_ns() - start_time) / NSEC_PER_USEC;
		kp_verbose_printf(ks, "booting time: %d (us)\n", delta_time);

		/* enter vm */
		kp_vm_call_proto(ks, pt);
	}

 out:
	kp_vm_exit(ks);
	return ret;
}


static void print_version(void)
{
}

static long ktap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	ktap_option_t parm;
	int ret;

	switch (cmd) {
	case KTAP_CMD_IOC_VERSION:
		print_version();
		return 0;
	case KTAP_CMD_IOC_RUN:
		/*
		 * must be root to run ktap script (at least for now)
		 *
		 * TODO: check perf_paranoid sysctl and allow non-root user
		 * to use ktap for tracing process(like uprobe) ?
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;

		ret = copy_from_user(&parm, (void __user *)arg,
				     sizeof(ktap_option_t));
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

int (*kp_ftrace_profile_set_filter)(struct perf_event *event, int event_id,
				    const char *filter_str);

struct syscall_metadata **syscalls_metadata;

/*TODO: kill this function in future */
static int __init init_dummy_kernel_functions(void)
{
	unsigned long *addr;

	/*
	 * ktap need symbol ftrace_profile_set_filter to set event filter, 
	 * export it in future. 
	 */
#ifdef CONFIG_PPC64
	kp_ftrace_profile_set_filter =
		(void *)kallsyms_lookup_name(".ftrace_profile_set_filter");
#else
	kp_ftrace_profile_set_filter =
		(void *)kallsyms_lookup_name("ftrace_profile_set_filter");
#endif
	if (!kp_ftrace_profile_set_filter) {
		pr_err("ktap: cannot lookup ftrace_profile_set_filter "
			"in kallsyms\n");
		return -1;
	}

	/* use syscalls_metadata for syscall event handling */
	addr = (void *)kallsyms_lookup_name("syscalls_metadata");
	if (!addr) {
		pr_err("ktap: cannot lookup syscalls_metadata in kallsyms\n");
		return -1;
	}

	syscalls_metadata = (struct syscall_metadata **)*addr;
	return 0;
}

static int __init init_ktap(void)
{
	struct dentry *ktapvm_dentry;

	if (init_dummy_kernel_functions())
		return -1;

	kp_dir_dentry = debugfs_create_dir("ktap", NULL);
	if (!kp_dir_dentry) {
		pr_err("ktap: debugfs_create_dir failed\n");
		return -1;
	}

	ktapvm_dentry = debugfs_create_file("ktapvm", 0444, kp_dir_dentry, NULL,
					    &ktapvm_fops);

	if (!ktapvm_dentry) {
		pr_err("ktapvm: cannot create ktapvm file\n");
		debugfs_remove_recursive(kp_dir_dentry);
		return -1;
	}

	return 0;
}

static void __exit exit_ktap(void)
{
	debugfs_remove_recursive(kp_dir_dentry);
}

module_init(init_ktap);
module_exit(exit_ktap);

MODULE_AUTHOR("Jovi Zhangwei <jovi.zhangwei@gmail.com>");
MODULE_DESCRIPTION("ktap");
MODULE_LICENSE("GPL");

int kp_max_loop_count = 100000;
module_param_named(max_loop_count, kp_max_loop_count, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_loop_count, "max loop execution count");

