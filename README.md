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
- register based interpreter(heavy optimized) in Linux kernel
- small and lightweight(5KLOC of interpreter)
- safty in sandbox
- easy to use in embedd environment even without debugging info
- a pure scripting interface for Linux tracing subsystem
- support static tracepoint, k(ret)probe, u(ret)probe, function trace, timer, backtrace and more

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
1) simplest one-line command to enable all tracepoints  

	ktap -e 'trace "*:*" function (e) { print(e) }'

2) function tracing  

	ktap -e 'trace "ftrace:function" function (e) { print(pid(), e) }'

3) simple syscall tracing  

	#scripts/syscalls.kp
	trace "syscalls:*" function (e) {
		print(cpu(), pid(), execname(), e)
	}

4) syscall tracing in histogram style  

	#scripts/syscalls_histogram.kp
	hist = {}

	trace "syscalls:sys_enter_*" function (e) {
		    table_count(hist, e.name)
	}

	trace_end function () {
		    histogram(hist)
	}

5) kprobe tracing  

	#scripts/kprobes-do-sys-open.kp
	trace "probe:do_sys_open dfd=%di filename=%dx flags=%cx mode=+4($stack)" function (e) {
		print("entry:", execname(), e)
	}

	trace "probe:do_sys_open%return fd=$retval" function (e) {
		print("exit:", execname(), e)
	}


6) uprobe tracing  

	#scripts/uprobes-malloc.kp
	#do not use 0x000773c0 in your system,
	#you need to calculate libc malloc symbol offset in your own system.
	#symbol resolve will support in future

	trace "probe:/lib/libc.so.6:0x000773c0" function (e) {
		print("entry:", execname(), e)
	}

	trace "probe:/lib/libc.so.6:0x000773c0%return" function (e) {
		print("exit:", execname(), e)
	}

Mailing list
------------
ktap@freelists.org  
You can subscribe KTAP mailing list at link(subscribe before posting):
http://www.freelists.org/list/ktap


License
-------
GPL v2


Contribution
------------
KTAP is still under active development, so contribution is welcome.
You are encouraged to report bugs, provide feedback, send feature request,
or hack on it.


ktap links
----------
LWN review on ktap by Jonathan Corbet: http://lwn.net/Articles/551314/  
presentation slides of LinuxCon Japan 2013: http://events.linuxfoundation.org/sites/events/files/lcjpcojp13_zhangwei.pdf


