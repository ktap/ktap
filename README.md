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
doesn't require compiling kernel module for each script, safe to use in
production environment, fulfilling the embedd ecosystem's tracing needs.

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

3) Load ktapvm kernel module(make sure debugfs mounted)  

	[root@jovi]# make load  #need to be root or have sudo access

4) Running ktap  

	[root@jovi]# ./ktap scripts/syscalls.kp


Examples
-------------------------------------
1) simplest one-liner command to enable all tracepoints  

	ktap -e 'trace "*:*" function () { print(argevent) }'

2) syscall tracing on target process  

	ktap -e 'trace "syscalls:*" function () { print(argevent) }' -- ls

3) function tracing  

	ktap -e 'trace "ftrace:function" function () { print(argevent) }'  

	ktap -e 'trace "ftrace:function /ip==mutex*/" function () { print(argevent) }'

4) simple syscall tracing  

	#scripts/syscalls.kp
	trace "syscalls:*" function () {
		print(cpu(), pid(), execname(), argevent)
	}

5) syscall tracing in histogram style  

	#scripts/syscalls_histogram.kp
	hist = {}

	trace "syscalls:sys_enter_*" function () {
		    table_count(hist, argname)
	}

	trace_end function () {
		    histogram(hist)
	}

6) kprobe tracing  

	#scripts/kprobes-do-sys-open.kp
	trace "probe:do_sys_open dfd=%di filename=%dx flags=%cx mode=+4($stack)" function () {
		print("entry:", execname(), argevent)
	}

	trace "probe:do_sys_open%return fd=$retval" function () {
		print("exit:", execname(), argevent)
	}


7) uprobe tracing  

	#scripts/uprobes-malloc.kp
	#do not use 0x000773c0 in your system,
	#you need to calculate libc malloc symbol offset in your own system.
	#symbol resolve will support in future

	trace "probe:/lib/libc.so.6:0x000773c0" function () {
		print("entry:", execname(), argevent)
	}

	trace "probe:/lib/libc.so.6:0x000773c0%return" function () {
		print("exit:", execname(), argevent)
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


links
-----
See doc/references.txt


