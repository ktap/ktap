ktap
====

A New Scripting Dynamic Tracing Tool For Linux

KTAP is a new scripting dynamic tracing tool for Linux,
it uses a scripting language and lets users trace the Linux kernel dynamically.
KTAP is designed to give operational insights with interoperability
that allows users to tune, troubleshoot and extend kernel and application.
It's similar with Linux Systemtap and Solaris Dtrace.

KTAP has different design principles from Linux mainstream dynamic tracing
language in that it's based on bytecode, so it doesn't depend upon GCC,
doesn't require compiling kernel module for each script, safe to use in
production environment, fulfilling the embedded ecosystem's tracing needs.

More information can be found at doc/ directory.


Highlights
----------
- simple but powerful scripting language
- register based interpreter (heavily optimized) in Linux kernel
- small and lightweight (6KLOC of interpreter)
- not depend on gcc for each script running
- easy to use in embedded environment without debugging info
- support for static tracepoint, k(ret)probe, u(ret)probe, function trace, timer, backtrace and more
- supported in x86, arm, ppc, mips
- safety in sandbox

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

	[root@jovi]# ./ktap scripts/helloworld.kp


Examples
--------
1) simplest one-liner command to enable all tracepoints  

	ktap -e "trace *:* { print(argevent) }"

2) syscall tracing on target process  

	ktap -e "trace syscalls:* { print(argevent) }" -- ls

3) function tracing  

	ktap -e "trace ftrace:function { print(argevent) }"

	ktap -e "trace ftrace:function /ip==mutex*/ { print(argevent) }"

4) simple syscall tracing  

	#scripts/syscalls.kp
	trace syscalls:* {
		print(cpu(), pid(), execname(), argevent)
	}

5) syscall tracing in histogram style  

	#scripts/syscalls_histogram.kp
	hist = {}

	trace syscalls:sys_enter_* {
		    table_count(hist, argname)
	}

	trace_end {
		    histogram(hist)
	}

6) kprobe tracing  

	#scripts/kprobes-do-sys-open.kp
	trace probe:do_sys_open dfd=%di fname=%dx flags=%cx mode=+4($stack) {
		print("entry:", execname(), argevent)
	}

	trace probe:do_sys_open%return fd=$retval {
		print("exit:", execname(), argevent)
	}


7) uprobe tracing  

	#scripts/uprobes-malloc.kp
	#do not use 0x000773c0 in your system,
	#you need to calculate libc malloc symbol offset in your own system.
	#symbol resolve will support in future

	trace probe:/lib/libc.so.6:0x000773c0 {
		print("entry:", execname(), argevent)
	}

	trace probe:/lib/libc.so.6:0x000773c0%return {
		print("exit:", execname(), argevent)
	}

8) timer  

	tick-1ms {
		printf("time fired on one cpu\n");
	}

	profile-2s {
		printf("time fired on every cpu\n");
	}


Mailing list
------------
ktap@freelists.org  
You can subscribe to KTAP mailing list at link (subscribe before posting):
http://www.freelists.org/list/ktap


Copyright and License
---------------------
KTAP is licensed under GPL v2

Copyright (C) 2012-2013 The ktap Project Developers.
All rights reserved.

Author: zhangwei(Jovi) <jovi.zhangwei@gmail.com>


Contribution
------------
KTAP is still under active development, so contributions are welcome.
You are encouraged to report bugs, provide feedback, send feature request,
or hack on it.


Links
-----
Some presentations of KTAP is available in doc/references.txt

