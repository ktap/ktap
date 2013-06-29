ktap
====

A New Scripting Dynamic Tracing Tool For Linux

KTAP is a new scripting dynamic tracing tool for Linux,
it uses a scripting language and lets users trace the Linux kernel dynamically.
KTAP is designed to give operational insights with interoperability
that allow users to tune, troubleshoot and extend kernel and application.
It's similar with Linux Systemtap and Solaris Dtrace.

KTAP have different design principles from Linux mainstream dynamic tracing
language in that it's based on bytecode, so it doesn't depend upon GCC,
doesn't require compiling a kernel module, safe to use in production
environment, fulfilling the embedd ecosystem's tracing needs.

KTAP also is designed for enabling great interoperability with Linux kernel,
it gives user the power to modify and extend the system, and let users
explore the system in an easy way.

More information can be found at doc/ directory.


Highlights
----------

- simple but powerful script language(forked by lua, proven to be fast)
- register based virtual machine in Linux kernel
- small and lightweight
- safty in sandbox
- support static tracepoint, kprobe, kretprobe, uprobe, uretprobe, timer, dumpstack

Building & Running
------------------

1) Clone ktap from github  

	[root@jovi]# git clone http://github.com/ktap/ktap.git

2) Compiling ktap  

	[root@jovi]# cd ktap
	[root@jovi]# make       #generate ktapvm kernel module and ktap binary

3) Insert ktapvm kernel module  

	[root@jovi]# insmod ./ktapvm.ko

4) Running ktap  

	[root@jovi]# ./ktap scripts/syscalls.kp


Examples
-------------------------------------

1) simple syscall tracing  

	[root@jovi]# cat scripts/syscalls.kp
	trace "syscalls:*" function (e) {
		printf("%d %d\t%s\t%s", cpu(), pid(), execname(), e.tostring())
	}

	[root@jovi]# ktap scripts/syscalls.kp
	0 895   sshd    sys_write(fd: 3, buf: b7fdf378, count: 90)
	0 895   sshd    sys_write -> 0x90
	0 895   sshd    sys_select(n: b, inp: b7fcc0f8, outp: b7fcc0e8, exp: 0, tvp: 0)
	1 602   iscsid  sys_poll -> 0x0
	1 602   iscsid  sys_poll(ufds: bf82fa10, nfds: 2, timeout_msecs: fa)
	0 895   sshd    sys_select -> 0x1
	0 895   sshd    sys_select(n: b, inp: b7fcc0f8, outp: b7fcc0e8, exp: 0, tvp: 0)
	...

2) histogram style syscall tracing  

	[root@jovi]# cat scripts/syscalls_histogram.kp
	hist = {}

	trace "syscalls:sys_enter_*" function (e) {
		    count(hist, e.name)
	}

	trace_end function () {
		    histogram(hist)
	}

	[root@jovi]# ktap scripts/syscalls_histogram.kp
	Press Control-C to stop.
	^C
                          value ------------- Distribution ------------- count
        sys_enter_rt_sigprocmask |@@@@@@@@@@@@@@@                        596
                sys_enter_select |@@@@                                   179
          sys_enter_rt_sigaction |@@@@                                   165
                  sys_enter_read |@@@                                    130
                 sys_enter_write |@@@                                    129
                  sys_enter_poll |                                       38
               sys_enter_lstat64 |                                       36
                 sys_enter_ioctl |                                       36
                sys_enter_capget |                                       31
              sys_enter_getxattr |                                       31
            sys_enter_mmap_pgoff |                                       28
                 sys_enter_close |                                       21
               sys_enter_fstat64 |                                       15
                  sys_enter_open |                                       15
                sys_enter_stat64 |                                       12
                  sys_enter_time |                                       11
             sys_enter_nanosleep |                                       10
              sys_enter_mprotect |                                       9
			     ...

3) kprobe tracing (do_sys_open)

	[root@jovi]# cat scripts/kprobes-do-sys-open.kp
	trace "probe:do_sys_open dfd=%di filename=%dx flags=%cx mode=+4($stack)" function (e) {
		printf("%20s\tentry:\t%s", execname(), e.tostring())
	}

	trace "probe:do_sys_open%return fd=$retval" function (e) {
		printf("%20s\texit:\t%s", execname(), e.tostring())
	}


4) uprobe tracing (malloc)

	[root@jovi]# cat scripts/uprobes-malloc.kp
	#do not use 0x000773c0 in your system,
	#you need to calculate libc malloc symbol offset in your own system.
	#symbol resolve will support in future

	trace "probe:/lib/libc.so.6:0x000773c0" function (e) {
		printf("%20s\tentry:\t%s", execname(), e.tostring())
	}

	trace "probe:/lib/libc.so.6:0x000773c0%return" function (e) {
		printf("%20s\texit:\t%s", execname(), e.tostring())
	}

Mailing list
------------
ktap@freelists.org  
You can subscribe KTAP mailing list at link(subscribe before posting):
http://www.freelists.org/list/ktap


License
-------
ktap is GPL licensed.  


Contribution
------------
KTAP is still under active development, so contribution is welcome.
You are encouraged to report bugs, provide feedback, send feature request,
or hack on it.


ktap links
----------
LWN review on ktap by Jonathan Corbet: http://lwn.net/Articles/551314/  
presentation slides of LinuxCon Japan 2013: http://events.linuxfoundation.org/sites/events/files/lcjpcojp13_zhangwei.pdf


