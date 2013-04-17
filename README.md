ktap
====

A New Scripting Dynamic Tracing Tool For Linux

KTAP is a new dynamic tracing tool for Linux,
it is designed to give operational insights that allow users to
tune and troubleshoot kernel and application.
It's similar with Linux Systemtap and Solaris Dtrace.

KTAP uses a scripting language and lets users trace the Linux kernel dynamically.
KTAP have different design principles from SystemTap in that it's based on bytecode,
so it doesn't depend upon GCC, doesn't require compiling a kernel module,
have great portability, safe to use in production environment,
fulfilling the embedd ecosystem's tracing needs.

KTAP is built from scratch, with GPL licensed.  
https://github.com/ktap/ktap.git

More information can be found at doc/ directory.

Building & Running
------------------

1) Clone ktap into linux/kernel/trace/  

	[root@jovi]# cd linux/kernel/trace/
	[root@jovi]# git clone https://github.com/ktap/ktap.git

2) Compiling ktap  

	[root@jovi]# cd linux/kernel/trace/ktap
	[root@jovi]# make ktap	--- generate userspace ktap tool
	[root@jovi]# make	--- generate ktapvm kernel module

3) Insert ktapvm kernel module  

	[root@jovi]# insmod ./ktapvm.ko

4) Running ktap  

	[root@jovi]# ./ktap scripts/syscalls.kp

Example of syscall tracing
--------------------------

syscalls.kp:  

	function eventfun (e) {
		printf("%d %d\t%s\t%s", cpu(), pid(), execname(), e.annotate)
	}

	os.probe("tp:syscalls", eventfun)

	os.probe_end(function () {
		printf("probe end\n")
	})


